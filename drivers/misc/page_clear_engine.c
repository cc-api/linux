// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Intel Corporation */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

static int engine_order = 5;		/* 128KB page allocations */
module_param(engine_order, int, 0644);
MODULE_PARM_DESC(engine_order, "The order of the page allocation for the engine");

static int engine_low_water = 3000;	/* When to ask for more pages */
module_param(engine_low_water, int, 0644);
MODULE_PARM_DESC(engine_low_water, "The low water of page count for the engine");

static bool engine_flag_cc = true;
module_param(engine_flag_cc, bool, 0644);
MODULE_PARM_DESC(engine_flag_cc, "Toggle cache control flag on/off for the engine");

static struct nodedata {
	struct dma_chan		*dma_chan;
} *nodedata;

struct perzone {
	struct list_head	pages;
	int			page_index;
	long			page_count;
	struct page		*newpage;
	int			node;
	dma_addr_t		dst_dma;
	dma_cookie_t		cookie;
};

static void *alloc_engine_descriptor(int node)
{
	struct perzone *pz;

	pz = kzalloc_node(sizeof(*pz), GFP_KERNEL, node);
	if (!pz)
		return NULL;

	INIT_LIST_HEAD(&pz->pages);
	pz->node = node;
	pz->newpage = NULL;
	pz->cookie = 0;

	return pz;
}

/* Called with zone->lock held */
static int get_clear_pages(void *v, int want, struct list_head *l, int *countp)
{
	struct perzone *pz = v;
	int node = pz->node;
	struct dma_chan *dma_chan = nodedata[node].dma_chan;
	struct device *dev = dmaengine_get_dma_device(dma_chan);
	struct page *page;
	int status;
	int migratetype;

	if (pz->newpage) {
		status = dma_async_is_tx_complete(dma_chan, pz->cookie);
		if (status == DMA_IN_PROGRESS) {
			/* Do nothing, will check status again next time */
			goto in_progress;
		} else if (status == DMA_COMPLETE || status > 0) {
			if (list_empty(&pz->pages))
				pz->page_index = 1 << engine_order;
			dma_unmap_page(dev, pz->dst_dma,
				       PAGE_SIZE << engine_order, DMA_FROM_DEVICE);

			if (unlikely(status > 0)) {
				/*
				 * Unlikely DMA error.
				 * Recover action: clear the page with memset().
				 */
				memset(page_to_virt(pz->newpage), 0, PAGE_SIZE << engine_order);
				pr_info("oops ... DMA error\n");
			}

			list_add_tail(&pz->newpage->lru, &pz->pages);
			pz->newpage = NULL;
			pz->cookie = 0;
		}
	}

in_progress:
	while (want > 0) {
		if (list_empty(&pz->pages))
			break;
		page = list_first_entry(&pz->pages, struct page, lru);
		/* Get migrate type of the large page */
		migratetype = page->index;
		if (--pz->page_index == 0) {
			list_del(&page->lru);
			pz->page_index = 1 << engine_order;
		} else {
			page += pz->page_index;
		}

		/* Make pcppage migrate type match type of large page */
		page->index = migratetype;
		list_add(&page->lru, l);
		(*countp)++;
		pz->page_count--;
		__count_zid_vm_events(PGALLOC_CLEAR_IN_PROGRESS, page_zonenum(page), -1);
		want--;
	}

	return (pz->page_count < engine_low_water && !pz->newpage) ? engine_order : 0;
}

/* Called with zone->lock held */
static void add_new_page(void *v, struct page *page)
{
	struct perzone *pz = v;
	int node = pz->node;
	struct dma_chan *dma_chan = nodedata[node].dma_chan;
	struct device *dev = dmaengine_get_dma_device(dma_chan);
	struct dma_async_tx_descriptor *tx = NULL;
	unsigned long dma_flags = 0;
	dma_cookie_t cookie;

	pz->page_count += 1 << engine_order;
	__count_zid_vm_events(PGALLOC_CLEAR_IN_PROGRESS, page_zonenum(page), 1 << engine_order);
	pz->newpage = page;

	pz->dst_dma = dma_map_page(dev, pz->newpage, 0, PAGE_SIZE << engine_order, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, pz->dst_dma))
		goto err_map;

	if (!engine_flag_cc)
		dma_flags |= DMA_PREP_NONTEMPORAL;

	tx = dmaengine_prep_dma_memset(dma_chan, pz->dst_dma, 0,
				       PAGE_SIZE << engine_order, dma_flags);
	if (!tx)
		goto err_prep;

	cookie = dmaengine_submit(tx);
	if (dma_submit_error(cookie)) {
		pr_info("oops ... couldn't submit DMA descriptor\n");
		goto err_prep;
	}
	pz->cookie = cookie;
	dma_async_issue_pending(dma_chan);

	return;

err_prep:
	dma_unmap_page(dev, pz->dst_dma, PAGE_SIZE << engine_order, DMA_FROM_DEVICE);
err_map:
	pz->page_count -= 1 << engine_order;
	__free_pages(page, engine_order);
	__count_zid_vm_events(PGALLOC_CLEAR_IN_PROGRESS, page_zonenum(page), -(1 << engine_order));
	pz->newpage = NULL;
}

static int engine_cleanup(void *v)
{
	struct page *page, *tmp;
	struct perzone *pz;
	int node;
	struct dma_chan *dma_chan;
	struct device *dev;
	int status;
	unsigned long timeout = jiffies + msecs_to_jiffies(5000);

	if (!v)
		return 0;

	pz = v;
	node = pz->node;
	dma_chan = nodedata[node].dma_chan;
	dev = dmaengine_get_dma_device(dma_chan);

	/*
	 * If a large page is being cleared by DMA device, wait for completion
	 * and then free the page.
	 */
	if (pz->newpage) {
		do {
			status = dma_async_is_tx_complete(dma_chan, pz->cookie);
			if (status == DMA_COMPLETE ||
			    (status > 0 && status != DMA_IN_PROGRESS) ||
			    time_after_eq(jiffies, timeout))
				break;
			msleep(1);
		} while (1);

		dma_unmap_page(dev, pz->dst_dma, PAGE_SIZE << engine_order, DMA_FROM_DEVICE);
		__free_pages(pz->newpage, engine_order);
		__count_zid_vm_events(PGALLOC_CLEAR_IN_PROGRESS, page_zonenum(pz->newpage),
				      -(1 << engine_order));
	}

	/*
	 * If there is a page in the process of being broken up, free up
	 * all the remaining pieces.
	 */
	if (!list_empty(&pz->pages)) {
		page = list_first_entry(&pz->pages, struct page, lru);
		while (--pz->page_index >= 0) {
			if (pz->page_index == 0)
				list_del(&page->lru);
			__free_page(page + pz->page_index);
			__count_zid_vm_events(PGALLOC_CLEAR_IN_PROGRESS, page_zonenum(page), -1);
		}
	}

	list_for_each_entry_safe(page, tmp, &pz->pages, lru) {
		list_del(&page->lru);
		__free_pages(page, engine_order);
		__count_zid_vm_events(PGALLOC_CLEAR_IN_PROGRESS, page_zonenum(page),
				      -(1 << engine_order));
	}

	kfree(pz);

	return 0;
}

static const struct page_clear_engine_ops page_engine_ops = {
	.create = alloc_engine_descriptor,
	.getpages = get_clear_pages,
	.provide = add_new_page,
	.clean = engine_cleanup,
};

static bool engine_filter_fn(struct dma_chan *chan, void *node)
{
	return dev_to_node(&chan->dev->device) == (int)(unsigned long)node;
}

static int get_dma_chan(int node)
{
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMSET, mask);

	nodedata[node].dma_chan = dma_request_channel(mask, engine_filter_fn,
						      (void *)(unsigned long)node);
	if (!nodedata[node].dma_chan) {
		pr_info("Cannot get DMA channel on node %d\n", node);
		return -ENODEV;
	}

	return 0;
}

static int __init init_page_clear_engine(void)
{
	int node, max_node = -1;
	int ret;

	for_each_online_node(node)
		if (node > max_node)
			max_node = node;

	nodedata = kcalloc(max_node + 1, sizeof(*nodedata), GFP_KERNEL);
	if (!nodedata)
		return -ENOMEM;

	for_each_online_node(node) {
		ret = get_dma_chan(node);
		if (ret)
			goto fail;
	}

	ret = register_page_clear_engine(&page_engine_ops);
	if (ret == 0)
		return ret;
fail:
	for (node = 0; node <= max_node; node++) {
		if (nodedata[node].dma_chan)
			dma_release_channel(nodedata[node].dma_chan);
	}

	kfree(nodedata);

	return ret;
}
module_init(init_page_clear_engine);

static void __exit exit_page_clear_driver(void)
{
	int node;

	unregister_page_clear_engine(&page_engine_ops);

	for_each_online_node(node)
		dma_release_channel(nodedata[node].dma_chan);

	kfree(nodedata);
}
module_exit(exit_page_clear_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Page clear engine with DMA offload support");
