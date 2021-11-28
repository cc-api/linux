// SPDX-License-Identifier: GPL-2.0
/*
 * intel-tpmi
 *
 * Copyright (c) 2021, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include <linux/auxiliary_bus.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/intel_tpmi.h>
#include "vsec.h"

/**
 * struct intel_tpmi_pm_feature - TPMI PM Feature Structure (PFS)
 * @tpmi_id:	This field indicates the nature and format of the TPMI feature
 *		structure.
 * @num_entries: Number of entries. Describes the number of feature interface
 *		 instances that exist in the PFS. This represents the maximum
 *		 number of Punits (i.e. superset chop) of all SKUs.
 * @entry_size:	Describe the entry size for each interface instance in
 *		32-bit words.
 * @cap_offset:	Specify the upper 16 bits of the 26 bits Cap Offset
 *		(i.e. Cap Offset is in KB unit) from the PM_Features base
 *		address to point to the base of the PM VSEC register bank.
 * @attribute:	Specify the attribute of this feature. 0x0=BIOS. 0x1=OS. 0x2-
 *		0x3=Reserved. OS/driver can choose to hide the MMIO region if
 *		Attribute=0x0.
 * @vsec_offset: This is calculated offset from vsec memory base to
 *		 cap_offset.
 * Stores one PFS which has instance for each TPMI feature.
 */
struct intel_tpmi_pm_feature {
	unsigned int tpmi_id;
	unsigned int num_entries;
	unsigned int entry_size;
	unsigned int cap_offset;
	unsigned int attribute;
	unsigned int vsec_offset;
	struct intel_vsec_device *vsec_dev;
};

struct intel_tpmi_info {
	struct intel_tpmi_pm_feature *tpmi_features;
	struct intel_vsec_device *vsec_dev;
	int feature_count;
	u64 pfs_start;
	struct intel_tpmi_plat_info plat_info;
	void __iomem *tpmi_control_mem;
};

enum intel_tpmi_id {
	TPMI_ID_RAPL = 0,
	TPMI_ID_PEM = 1,
	TPMI_ID_UFS = 2,
	TPMI_ID_SST = 5,
};

/* TPMI Control Interface */
#define TPMI_CONTROL_ID		0x80
#define TPMI_INTERFACE_OFFSET	0x00
#define TPMI_COMMAND_OFFSET	0x08
#define TPMI_DATA_OFFSET	0x0C
#define CONTROL_TIMEOUT_US	5000

#define tpmi_to_dev(info)	&info->vsec_dev->pcidev->dev
static DEFINE_IDA(intel_vsec_tpmi_ida);

static void tpmi_set_control_base(struct intel_tpmi_info *tpmi_info,
				  struct intel_tpmi_pm_feature *pfs)
{
	void __iomem *mem;
	u16 size;

	size = pfs->num_entries * pfs->entry_size * 4;
	mem = ioremap(pfs->vsec_offset, size);
	if (!mem)
		return;

	/* mem is pointing to TPMI CONTROL base */
	tpmi_info->tpmi_control_mem = mem;
}

static int tpmi_get_feature_status(struct intel_tpmi_info *tpmi_info,
				   int feature_id, int *locked, int *disabled)
{
	struct intel_vsec_device *vsec_dev = tpmi_info->vsec_dev;
	u32 interface, data;
	s64 tm_delta = 0;
	ktime_t tm;
	int ret;

	if (!tpmi_info->tpmi_control_mem)
		return -EFAULT;

	pm_runtime_get_sync(&vsec_dev->auxdev.dev);
	/* Poll for rb bit == 0 */
	tm = ktime_get();
	do {
		interface = readl(tpmi_info->tpmi_control_mem +
				  TPMI_INTERFACE_OFFSET);
		if (interface & BIT(0)) {
			ret = -EBUSY;
			tm_delta = ktime_us_delta(ktime_get(), tm);
			if (tm_delta > 1000)
				cond_resched();
			continue;
		}
		ret = 0;
		break;
	} while (tm_delta < CONTROL_TIMEOUT_US);

	pr_debug("interface reg:%x\n", interface);

	if (ret)
		return ret;

	pr_debug("ready: Run busy is 0\n");
	/* cmd == 0x10 for TPMI_GET_STATE */
	writel(0x10, tpmi_info->tpmi_control_mem + TPMI_COMMAND_OFFSET);
	pr_debug("tpmi_cmd: %x\n", readl(tpmi_info->tpmi_control_mem + TPMI_COMMAND_OFFSET));

	writel(feature_id << 8, tpmi_info->tpmi_control_mem +
		TPMI_DATA_OFFSET); /* data = feature_id */

	pr_debug("tpmi_data: %x\n", readl(tpmi_info->tpmi_control_mem + TPMI_DATA_OFFSET));

	writel(BIT(0) | (1 << 16),  tpmi_info->tpmi_control_mem + TPMI_INTERFACE_OFFSET);
	pr_debug("tpmi_interface: %x\n", readl(tpmi_info->tpmi_control_mem + TPMI_INTERFACE_OFFSET));

	/* Poll for rb bit == 0 */
	tm = ktime_get();
	do {
		interface = readl(tpmi_info->tpmi_control_mem +
				  TPMI_INTERFACE_OFFSET);
		if (interface & BIT_ULL(0)) {
			ret = -EBUSY;
			tm_delta = ktime_us_delta(ktime_get(), tm);
			if (tm_delta > 1000)
				cond_resched();
			continue;
		}
		ret = 0;
		break;
	} while (tm_delta < CONTROL_TIMEOUT_US);

	pr_debug("tpmi_interface after poll: %x\n", readl(tpmi_info->tpmi_control_mem + TPMI_INTERFACE_OFFSET));

	if (ret)
		goto done_proc;

	data = (interface << 8) & 0xff;
	if (data != 0x40)
		return -EBUSY;

	data = readl(tpmi_info->tpmi_control_mem + TPMI_DATA_OFFSET);
	pr_debug("tpmi_data result: %x\n", readl(tpmi_info->tpmi_control_mem + TPMI_DATA_OFFSET));

	*disabled = 0;
	*locked = 0;

	if (data & BIT(0))
		*disabled = 1;

	if (data & BIT(31))
		*locked = 1;

	ret = 0;

done_proc:
	pm_runtime_mark_last_busy(&vsec_dev->auxdev.dev);
	pm_runtime_put_autosuspend(&vsec_dev->auxdev.dev);

	return ret;
}

static int tpmi_update_pfs(struct intel_tpmi_pm_feature *pfs, u64 start,
			   int size)
{
	void __iomem *pfs_mem;
	u64 header;
	u8 *mem;

	pfs_mem = ioremap(start, size);
	if (!pfs_mem)
		return -ENOMEM;

	header = readq(pfs_mem);
	mem = (u8 *)&header;

	pfs->tpmi_id = *mem++;
	pfs->num_entries = *mem++;
	pfs->entry_size = *(u16 *)mem;
	mem += 2;
	pfs->cap_offset = *(u16 *)mem;
	mem += 2;
	pfs->attribute = (*mem & 0x03);

	iounmap(pfs_mem);

	return 0;
}

static const char *intel_tpmi_name(enum intel_tpmi_id id)
{
	switch (id) {
	case TPMI_ID_RAPL:
		return "rapl";
	case TPMI_ID_PEM:
		return "pem";
	case TPMI_ID_UFS:
		return "ufs";
	case TPMI_ID_SST:
		return "sst";
	default:
		return NULL;
	}
}

static int tpmi_create_device(struct intel_tpmi_info *tpmi_info,
			      struct intel_tpmi_pm_feature *pfs,
			      u64 pfs_start)
{
	struct intel_vsec_device *vsec_dev = tpmi_info->vsec_dev;
	struct intel_vsec_device *feature_vsec_dev;
	struct resource *res, *tmp;
	char feature_id_name[15];
	const char *name;
	int ret, i;

	name = intel_tpmi_name(pfs->tpmi_id);
	if (!name)
		return -ENOTSUPP;

	feature_vsec_dev = kzalloc(sizeof(*feature_vsec_dev), GFP_KERNEL);
	if (!feature_vsec_dev)
		return -ENOMEM;

	res = kcalloc(pfs->num_entries, sizeof(*res), GFP_KERNEL);
	if (!res) {
		ret = -ENOMEM;
		goto free_vsec;
	}

	snprintf(feature_id_name, sizeof(feature_id_name), "tpmi-%s", name);

	for (i = 0, tmp = res; i < pfs->num_entries; i++, tmp++) {
		tmp->start = pfs->vsec_offset + (pfs->entry_size * 4) * i;
		tmp->end = tmp->start + (pfs->entry_size * 4) - 1;
		tmp->flags = IORESOURCE_MEM;
		dev_dbg(tpmi_to_dev(tpmi_info), " TPMI id:%x Entry %d, %pr", pfs->tpmi_id,
			i, tmp);
	}

	feature_vsec_dev->pcidev = vsec_dev->pcidev;
	feature_vsec_dev->resource = res;
	feature_vsec_dev->num_resources = pfs->num_entries;
	feature_vsec_dev->priv_data = &tpmi_info->plat_info;
	feature_vsec_dev->priv_data_size = sizeof(tpmi_info->plat_info);
        feature_vsec_dev->ida = &intel_vsec_tpmi_ida;

	ret = intel_vsec_add_aux(vsec_dev->pcidev, &vsec_dev->auxdev.dev, feature_vsec_dev,
				 feature_id_name);
	if (ret)
		goto free_res;

	return 0;

free_res:
	kfree(res);
free_vsec:
	kfree(feature_vsec_dev);

	return ret;
}

static void tpmi_create_devices(struct intel_tpmi_info *tpmi_info)
{
	struct intel_vsec_device *vsec_dev = tpmi_info->vsec_dev;
	int ret, i;

	for (i = 0; i < vsec_dev->num_resources; i++) {
		struct intel_tpmi_pm_feature *pfs;
		int locked, disabled;

		pfs = &tpmi_info->tpmi_features[i];
		ret = tpmi_get_feature_status(tpmi_info, pfs->tpmi_id,
					      &locked, &disabled);
		if (!ret)
			dev_dbg(tpmi_to_dev(tpmi_info), "id:%d, locked:%d disabled:%d\n",
				pfs->tpmi_id, locked, disabled);
		/* Todo "continue" for locked/disabled fatures */
		ret = tpmi_create_device(tpmi_info, &tpmi_info->tpmi_features[i],
					 tpmi_info->pfs_start);
		if (ret)
			continue;
	}
}

#define TPMI_INFO_ID 0x81
#define TPMI_INFO_BUS_INFO_OFFSET 0x08

static int tpmi_process_info(struct intel_tpmi_info *tpmi_info,
			     struct intel_tpmi_pm_feature *pfs)
{
	int fn, dev, bus, pkg_id;
	void __iomem *info_mem;
	u64 info;
	u8 *mem;

	info_mem = ioremap(pfs->vsec_offset + TPMI_INFO_BUS_INFO_OFFSET,
			   pfs->entry_size * 4 - TPMI_INFO_BUS_INFO_OFFSET);
	if (!info_mem)
		return -ENOMEM;

	info = readq(info_mem);

	mem = (u8 *)&info;

	fn = *mem;
	dev = (fn >> 3) & 0xff;
	fn = fn & 0x07;
	mem++;
	bus = *mem++;
	pkg_id = *mem;

	dev_dbg(tpmi_to_dev(tpmi_info), "bus:%d dev:%d fn:%d pkg_id:%d\n", bus, dev, fn,
		pkg_id);
	tpmi_info->plat_info.package_id = pkg_id;
	tpmi_info->plat_info.bus_number = bus;
	tpmi_info->plat_info.device_number = dev;
	tpmi_info->plat_info.function_number = fn;

	iounmap(info_mem);

	return 0;
}

static int tpmi_get_resource(struct intel_vsec_device *vsec_dev, int index,
			     u64 *resource_start)
{
	struct resource *res;
	int size;

	res = &vsec_dev->resource[index];
	if (!res)
		return -EINVAL;

	size = resource_size(res);

	*resource_start = res->start;

	return size;
}

static int intel_vsec_tpmi_init(struct auxiliary_device *auxdev)
{
	struct intel_vsec_device *vsec_dev = auxdev_to_ivdev(auxdev);
	struct pci_dev *pci_dev = vsec_dev->pcidev;
	struct intel_tpmi_info *tpmi_info;
	u64 pfs_start = 0;
	int i;

	dev_dbg(&pci_dev->dev, "%s no_resource:%d\n", __func__,
		vsec_dev->num_resources);

	tpmi_info = devm_kzalloc(&auxdev->dev, sizeof(*tpmi_info), GFP_KERNEL);
	if (!tpmi_info)
		return -ENOMEM;

	tpmi_info->vsec_dev = vsec_dev;
	tpmi_info->feature_count = vsec_dev->num_resources;
	tpmi_info->plat_info.bus_number = pci_dev->bus->number;

	tpmi_info->tpmi_features = devm_kcalloc(&auxdev->dev, vsec_dev->num_resources,
						sizeof(*tpmi_info->tpmi_features),
						GFP_KERNEL);
	if (!tpmi_info->tpmi_features) {
		devm_kfree(&auxdev->dev, tpmi_info);
		return -ENOMEM;
	}

	for (i = 0; i < vsec_dev->num_resources; i++) {
		struct intel_tpmi_pm_feature *pfs;
		u64 res_start;
		int size, ret;

		pfs = &tpmi_info->tpmi_features[i];
		pfs->vsec_dev = vsec_dev;

		size = tpmi_get_resource(vsec_dev, i, &res_start);
		if (size < 0)
			continue;

		ret = tpmi_update_pfs(pfs, res_start, size);
		if (ret)
			continue;

		if (!pfs_start)
			pfs_start = res_start;

		pfs->cap_offset *= 1024;

		pfs->vsec_offset = pfs_start + pfs->cap_offset;

		dev_dbg(&pci_dev->dev, "PFS[tpmi_id=0x%x num_entries=0x%x entry_size=0x%x cap_offset=0x%x pfs->attribute=0x%x\n",
			 pfs->tpmi_id, pfs->num_entries, pfs->entry_size, pfs->cap_offset, pfs->attribute);

		/* Process TPMI_INFO to get BDF to package mapping */
		if (pfs->tpmi_id == TPMI_INFO_ID)
			tpmi_process_info(tpmi_info, pfs);

		if (pfs->tpmi_id == TPMI_CONTROL_ID)
			tpmi_set_control_base(tpmi_info, pfs);
	}

	tpmi_info->pfs_start = pfs_start;

	auxiliary_set_drvdata(auxdev, tpmi_info);

	tpmi_create_devices(tpmi_info);

	pm_runtime_enable(&auxdev->dev);
	pm_runtime_set_autosuspend_delay(&auxdev->dev, 2000);
	pm_runtime_use_autosuspend(&auxdev->dev);
	pm_runtime_put(&auxdev->dev);

	return 0;
}

static int tpmi_probe(struct auxiliary_device *auxdev,
		      const struct auxiliary_device_id *id)
{
	return intel_vsec_tpmi_init(auxdev);
}

static void tpmi_remove(struct auxiliary_device *auxdev)
{
	struct intel_tpmi_info *tpmi_info = auxiliary_get_drvdata(auxdev);

	if (tpmi_info->tpmi_control_mem)
		iounmap(tpmi_info->tpmi_control_mem);

	pm_runtime_get_sync(&auxdev->dev);
	pm_runtime_put_noidle(&auxdev->dev);
	pm_runtime_disable(&auxdev->dev);
}

static const struct auxiliary_device_id tpmi_id_table[] = {
	{ .name = "intel_vsec.tpmi" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, tpmi_id_table);

static struct auxiliary_driver tpmi_aux_driver = {
	.id_table	= tpmi_id_table,
	.remove		= tpmi_remove,
	.probe		= tpmi_probe,
};

static int __init tpmi_init(void)
{
	return auxiliary_driver_register(&tpmi_aux_driver);
}
module_init(tpmi_init);

static void __exit tpmi_exit(void)
{
	auxiliary_driver_unregister(&tpmi_aux_driver);
}
module_exit(tpmi_exit);

MODULE_DESCRIPTION("Intel TPMI enumeration module");
MODULE_LICENSE("GPL");
