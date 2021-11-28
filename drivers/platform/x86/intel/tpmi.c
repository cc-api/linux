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
};

struct intel_tpmi_info {
	struct intel_tpmi_pm_feature *tpmi_features;
	struct intel_vsec_device *vsec_dev;
	int feature_count;
	u64 pfs_start;
};

enum intel_tpmi_id {
	TPMI_ID_RAPL = 0,
	TPMI_ID_PEM = 1,
	TPMI_ID_UFS = 2,
	TPMI_ID_SST = 5,
};

#define tpmi_to_dev(info)	&info->vsec_dev->pcidev->dev
static DEFINE_IDA(intel_vsec_tpmi_ida);

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

		pfs = &tpmi_info->tpmi_features[i];
		ret = tpmi_create_device(tpmi_info, &tpmi_info->tpmi_features[i],
					 tpmi_info->pfs_start);
		if (ret)
			continue;
	}
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
	}

	tpmi_info->pfs_start = pfs_start;

	auxiliary_set_drvdata(auxdev, tpmi_info);

	tpmi_create_devices(tpmi_info);

	return 0;
}

static int tpmi_probe(struct auxiliary_device *auxdev,
		      const struct auxiliary_device_id *id)
{
	return intel_vsec_tpmi_init(auxdev);
}

static void tpmi_remove(struct auxiliary_device *auxdev)
{
	/*
	 * TODO: Remove processing by getting
	 * struct intel_tpmi_info *tpmi_info = auxiliary_get_drvdata(auxdev);
	 */
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
