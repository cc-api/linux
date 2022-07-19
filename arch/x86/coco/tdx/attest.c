// SPDX-License-Identifier: GPL-2.0
/*
 * attest.c - TDX guest attestation interface driver.
 *
 * Implements user interface to trigger attestation process.
 *
 * Copyright (C) 2022 Intel Corporation
 *
 */

#define pr_fmt(fmt) "x86/tdx: attest: " fmt

#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/set_memory.h>
#include <linux/mutex.h>
#include <asm/irq_vectors.h>
#include <asm/apic.h>
#include <asm/tdx.h>
#include <asm/coco.h>
#include <uapi/asm/tdx.h>

#define DRIVER_NAME "tdx-attest"

/* TDREPORT module call leaf ID */
#define TDX_GET_REPORT			4
/* GetQuote hypercall leaf ID */
#define TDVMCALL_GET_QUOTE             0x10002

/* Used for buffer allocation in GetQuote request */
struct quote_buf {
	/* vmapped address of kernel buffer (size is page aligned) */
	void *vmaddr;
	/* Number of pages */
	int count;
};

/* List entry of quote_list */
struct quote_entry {
	/* Flag to check validity of the GetQuote request */
	bool valid;
	/* Kernel buffer to share data with VMM */
	struct quote_buf buf;
	/* Completion object to track completion of GetQuote request */
	struct completion compl;
	struct list_head list;
};

static struct miscdevice miscdev;

/*
 * To support parallel GetQuote requests, use the list
 * to track active GetQuote requests.
 */
static LIST_HEAD(quote_list);

/* Lock to protect quote_list */
static DEFINE_MUTEX(quote_lock);

/*
 * Workqueue to handle Quote data after Quote generation
 * notification from VMM.
 */
struct workqueue_struct *quote_wq;
struct work_struct quote_work;

static long tdx_get_report(void __user *argp)
{
	void *reportdata = NULL, *tdreport = NULL;
	long ret;

	/* Allocate buffer space for REPORTDATA */
	reportdata = kmalloc(TDX_REPORTDATA_LEN, GFP_KERNEL);
	if (!reportdata)
		return -ENOMEM;

	/* Allocate buffer space for TDREPORT */
	tdreport = kmalloc(TDX_REPORT_LEN, GFP_KERNEL);
	if (!tdreport) {
		ret = -ENOMEM;
		goto out;
	}

	/* Copy REPORTDATA from the user buffer */
	if (copy_from_user(reportdata, argp, TDX_REPORTDATA_LEN)) {
		ret = -EFAULT;
		goto out;
	}

	/*
	 * Generate TDREPORT using "TDG.MR.REPORT" TDCALL.
	 *
	 * Get the TDREPORT using REPORTDATA as input. Refer to
	 * section 22.3.3 TDG.MR.REPORT leaf in the TDX Module 1.0
	 * Specification for detailed information.
	 */
	ret = __tdx_module_call(TDX_GET_REPORT, virt_to_phys(tdreport),
				virt_to_phys(reportdata), 0, 0, 0, 0, 0, 0, NULL);
	if (ret) {
		pr_debug("TDREPORT TDCALL failed, status:%lx\n", ret);
		ret = -EIO;
		goto out;
	}

	/* Copy TDREPORT back to the user buffer */
	if (copy_to_user(argp, tdreport, TDX_REPORT_LEN))
		ret = -EFAULT;

out:
	kfree(reportdata);
	kfree(tdreport);
	return ret;
}

#if 0
/* tdx_get_quote_hypercall() - Request to get TD Quote using TDREPORT */
static long tdx_get_quote_hypercall(struct quote_buf *buf)
{
	struct tdx_hypercall_args args = {0};

	args.r10 = TDX_HYPERCALL_STANDARD;
	args.r11 = TDVMCALL_GET_QUOTE;
	args.r12 = cc_mkdec(page_to_phys(vmalloc_to_page(buf->vmaddr)));
	args.r13 = buf->count * PAGE_SIZE;

	/*
	 * Pass the physical address of TDREPORT to the VMM and
	 * trigger the Quote generation. It is not a blocking
	 * call, hence completion of this request will be notified to
	 * the TD guest via a callback interrupt. More info about ABI
	 * can be found in TDX Guest-Host-Communication Interface
	 * (GHCI), sec titled "TDG.VP.VMCALL<GetQuote>".
	 */
	return __tdx_hypercall(&args, 0);
}
#endif

static void print_prot_flags(void *addr)
{
	unsigned int level;
	pte_t *ptep, pte;
	struct page *_page;

	ptep = lookup_address((unsigned long)addr, &level);
	pte = *ptep;
	if (is_vmalloc_addr(addr))
		_page = vmalloc_to_page(addr);
	else
		_page = virt_to_page(addr);
	pr_info("page addr:%p pfn:%lx flags:%lx\n", addr, page_to_pfn(_page),
			pgprot_val(pte_pgprot(pte)));
}

/*
 * init_quote_buf() - Initialize the quote buffer by allocating
 *                    a shared buffer of given size.
 *
 * Size is page aligned and the allocated memory is decrypted
 * to allow VMM to access it. Uses VMAP to create a virtual
 * mapping, which is further used to create a shared mapping
 * for the buffer without affecting the direct map.
 */
static int init_quote_buf(struct quote_buf *buf, u64 req_size)
{
	int size = PAGE_ALIGN(req_size);
	void *addr = NULL, *vmaddr = NULL;
	int count = size >> PAGE_SHIFT;
	struct page **pages = NULL;
	int i;

	addr = alloc_pages_exact(size, GFP_KERNEL);
	if (!addr)
		return -ENOMEM;

	/* Allocate mem for array of page ptrs */
	pages = kcalloc(count, sizeof(*pages), GFP_KERNEL);
	if (!pages) {
		free_pages_exact(addr, size);
		return -ENOMEM;
	}

	for (i = 0; i < count; i++)
		pages[i] = virt_to_page(addr + i * PAGE_SIZE);

	print_prot_flags(addr);

	/*
	 * Use VMAP to create a virtual mapping, which is used
	 * to create shared mapping without affecting the
	 * direct map. Use VM_MAP_PUT_PAGES to allow vmap()
	 * responsible for freeing the pages when using vfree().
	 */
	vmaddr = vmap(pages, count, VM_MAP_PUT_PAGES, PAGE_KERNEL);
	if (!vmaddr) {
		kfree(pages);
		free_pages_exact(addr, size);
		return -EIO;
	}

	print_prot_flags(vmaddr);
	/* Use noalias variant to not affect the direct mapping */
	if (set_memory_decrypted_noalias((unsigned long)vmaddr, count)) {
		vfree(vmaddr);
		return -EIO;
	}
	print_prot_flags(vmaddr);
	print_prot_flags(addr);

	pr_info("Allocation done\n");

	buf->vmaddr = vmaddr;
	buf->count = count;

	return 0;
}

/* Remove the shared mapping and free the memory */
static void deinit_quote_buf(struct quote_buf *buf)
{
	/* Mark pages private */
	if (set_memory_encrypted_noalias((unsigned long)buf->vmaddr,
				buf->count)) {
		pr_warn("Failed to encrypt %d pages at %p", buf->count,
				buf->vmaddr);
		return;
	}

	vfree(buf->vmaddr);
}

static struct quote_entry *alloc_quote_entry(u64 buf_len)
{
	struct quote_entry *entry = NULL;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	/* Init buffer for quote request */
	if (init_quote_buf(&entry->buf, buf_len)) {
		kfree(entry);
		return NULL;
	}

	init_completion(&entry->compl);
	entry->valid = true;

	return entry;
}

static void free_quote_entry(struct quote_entry *entry)
{
	deinit_quote_buf(&entry->buf);
	kfree(entry);
}

/* Must be called with quote_lock held */
static void _del_quote_entry(struct quote_entry *entry)
{
	list_del(&entry->list);
	free_quote_entry(entry);
}

static void del_quote_entry(struct quote_entry *entry)
{
	mutex_lock(&quote_lock);
	_del_quote_entry(entry);
	mutex_unlock(&quote_lock);
}

/* Handles early termination of GetQuote requests */
void terminate_quote_request(struct quote_entry *entry)
{
	struct tdx_quote_hdr *quote_hdr;

	/*
	 * For early termination, if the request is not yet
	 * processed by VMM (GET_QUOTE_IN_FLIGHT), the VMM
	 * still owns the shared buffer, so mark the request
	 * invalid to let quote_callback_handler() handle the
	 * memory cleanup function. If the request is already
	 * processed, then do the cleanup and return.
	 */

	mutex_lock(&quote_lock);
	quote_hdr = (struct tdx_quote_hdr *)entry->buf.vmaddr;
	if (quote_hdr->status == GET_QUOTE_IN_FLIGHT) {
		entry->valid = false;
		mutex_unlock(&quote_lock);
		return;
	}
	_del_quote_entry(entry);
	mutex_unlock(&quote_lock);
}

static long tdx_get_quote(void __user *argp)
{
	struct quote_entry *entry;
	struct tdx_quote_req req;
	struct quote_buf *buf;
	long ret;

	pr_info("%s:%d Start()\n", __func__, __LINE__);

	/* Copy GetQuote request struct from user buffer */
	if (copy_from_user(&req, argp, sizeof(struct tdx_quote_req)))
		return -EFAULT;

	/* Make sure the length is valid */
	if (!req.len)
		return -EINVAL;

	entry = alloc_quote_entry(req.len);
	if (!entry)
		return -ENOMEM;

	buf = &entry->buf;

	/* Copy TDREPORT from user buffer to kernel Quote buffer */
	if (copy_from_user(buf->vmaddr, (void __user *)req.buf, req.len)) {
		free_quote_entry(entry);
		return -EFAULT;
	}

	mutex_lock(&quote_lock);

#if 0
	/* Submit GetQuote Request */
	ret = tdx_get_quote_hypercall(buf);
	if (ret) {
		mutex_unlock(&quote_lock);
		pr_err("GetQuote hypercall failed, status:%lx\n", ret);
		free_quote_entry(entry);
		return -EIO;
	}
#endif
	apic->send_IPI_all(TDX_GUEST_EVENT_NOTIFY_VECTOR);

	pr_info("%s:%d Hypercall done, queueing request\n", __func__, __LINE__);
	/* Add current quote entry to quote_list to track active requests */
	list_add_tail(&entry->list, &quote_list);

	mutex_unlock(&quote_lock);

	/* Wait for attestation completion */
	ret = wait_for_completion_interruptible(&entry->compl);
	if (ret < 0) {
		pr_info("%s:%d GetQuote callback timedout\n", __func__, __LINE__);
		terminate_quote_request(entry);
		return -EINTR;
	}

	pr_info("%s:%d Copying the result back to user\n", __func__, __LINE__);

	/*
	 * If GetQuote request completed successfully, copy the result
	 * back to the user and do the cleanup.
	 */
	if (copy_to_user((void __user *)req.buf, buf->vmaddr, req.len))
		ret = -EFAULT;

	/*
	 * Reaching here means GetQuote request is processed
	 * successfully. So do the cleanup and return 0.
	 */
	pr_info("%s:%d done(), status:%lx\n", __func__, __LINE__, ret);
	del_quote_entry(entry);

	return 0;
}

static void attestation_callback_handler(void)
{
	queue_work(quote_wq, &quote_work);
}

static void quote_callback_handler(struct work_struct *work)
{
	struct tdx_quote_hdr *quote_hdr;
	struct quote_entry *entry, *next;

	pr_info("%s:%d start()\n", __func__, __LINE__);

	/* Find processed quote request and mark it complete */
	mutex_lock(&quote_lock);
	list_for_each_entry_safe(entry, next, &quote_list, list) {
		quote_hdr = (struct tdx_quote_hdr *)entry->buf.vmaddr;
		if (quote_hdr->status == GET_QUOTE_IN_FLIGHT)
			continue;
		/*
		 * If user invalidated the current request, remove the
		 * entry from the quote list and free it. If the request
		 * is still valid, mark it complete.
		 */
		pr_info("%s:%d Complete current request valid:%d\n",
				__func__, __LINE__, entry->valid);
		if (entry->valid)
			complete(&entry->compl);
		else
			_del_quote_entry(entry);
	}
	mutex_unlock(&quote_lock);
	pr_info("%s:%d done()\n", __func__, __LINE__);
}

static long tdx_attest_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	long ret = -EINVAL;

	switch (cmd) {
	case TDX_CMD_GET_REPORT:
		ret = tdx_get_report(argp);
		break;
	case TDX_CMD_GET_QUOTE:
		ret = tdx_get_quote(argp);
		break;
	default:
		pr_debug("cmd %d not supported\n", cmd);
		break;
	}

	return ret;
}

static const struct file_operations tdx_attest_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= tdx_attest_ioctl,
	.llseek		= no_llseek,
};

static int __init tdx_attestation_init(void)
{
	int ret;

	/* Make sure we are in a valid TDX platform */
	if (!cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return -EIO;

	quote_wq = create_singlethread_workqueue("tdx_quote_handler");

	INIT_WORK(&quote_work, quote_callback_handler);

	/* Register attestation event notify handler */
	tdx_setup_ev_notify_handler(attestation_callback_handler);

	miscdev.name = DRIVER_NAME;
	miscdev.minor = MISC_DYNAMIC_MINOR;
	miscdev.fops = &tdx_attest_fops;

	ret = misc_register(&miscdev);
	if (ret) {
		pr_err("misc device registration failed\n");
		return ret;
	}

	return 0;
}
device_initcall(tdx_attestation_init)
