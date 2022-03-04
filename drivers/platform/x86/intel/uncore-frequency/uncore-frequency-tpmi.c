// SPDX-License-Identifier: GPL-2.0
/*
 * intel-ufs-tpmi: Intel x86 platform uncore frequency scaling
 *
 * Copyright (c) 2021, Intel Corporation.
 * All Rights Reserved.
 *
 */

#define DEBUG

#include <linux/auxiliary_bus.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/intel_tpmi.h>

#include "uncore-frequency-common.h"

#define	UFS_HEADER_VERSION		1
#define UFS_HEADER_INDEX		0
#define UFS_FABRIC_CLUSTER_OFFSET	8
#define UFS_FABRIC_CLUSTER_SIZE	4 * 8 /* status + control +adv_ctl1 + adv_ctl2 */

#define UFS_STATUS_INDEX		0
#define UFS_CONTROL_INDEX		8

#define UNCORE_FREQ_KHZ_MULTIPLIER	100000

struct tpmi_ufs_cluster_info {
	void __iomem *cluster_base;
	struct uncore_data uncore_data;
	struct auxiliary_device *auxdev;
	int offset;
};

struct tpmi_ufs_punit_info {
	void __iomem *ufs_base;
	int ufs_header_ver;
	int cluster_count;
	struct tpmi_ufs_cluster_info *cluster_infos;
};

struct tpmi_ufs_struct {
	int number_of_punits;
	struct tpmi_ufs_punit_info *punit_info;
};

static int uncore_read_control_freq(struct uncore_data *data, unsigned int *min,
				    unsigned int *max)
{
	struct tpmi_ufs_cluster_info *cluster_info;
	u64 control;

	cluster_info = container_of(data, struct tpmi_ufs_cluster_info, uncore_data);
	control = intel_tpmi_readq(cluster_info->auxdev, (u8 __iomem *)cluster_info->cluster_base + UFS_CONTROL_INDEX);
	pr_debug("%s  offset:%x read:%llx\n", __func__, UFS_STATUS_INDEX, control);
	control >>= 8;
	*max = (control & 0x7f) * UNCORE_FREQ_KHZ_MULTIPLIER;
	*min = ((control >> 7) & 0x7f) * UNCORE_FREQ_KHZ_MULTIPLIER;

	return 0;
}

static int uncore_write_control_freq(struct uncore_data *data, unsigned int input,
				     unsigned int min_max)
{
	struct tpmi_ufs_cluster_info *cluster_info;
	u64 control;

	input /= UNCORE_FREQ_KHZ_MULTIPLIER;
	if (!input || input > 0x7F) {
		return -EINVAL;
	}

	cluster_info = container_of(data, struct tpmi_ufs_cluster_info, uncore_data);
	control = intel_tpmi_readq(cluster_info->auxdev, (u8 __iomem *)cluster_info->cluster_base + UFS_CONTROL_INDEX);

	if (min_max) {
		control &= ~GENMASK(14, 8);
		control |= (input << 8);
	} else  {
		control &= ~GENMASK(21, 15);
		control |= (input << 15);
	}
	pr_debug("%s offset:%x write:%llx\n", __func__, UFS_STATUS_INDEX, control);
	intel_tpmi_writeq(cluster_info->auxdev, control, ((u8 __iomem *)cluster_info->cluster_base + UFS_CONTROL_INDEX));

	return 0;
}

static int uncore_read_freq(struct uncore_data *data, unsigned int *freq)
{
	struct tpmi_ufs_cluster_info *cluster_info;
	u64 status;

	cluster_info = container_of(data, struct tpmi_ufs_cluster_info, uncore_data);
	status = intel_tpmi_readq(cluster_info->auxdev, (u8 __iomem *)cluster_info->cluster_base + UFS_STATUS_INDEX);
	pr_debug("%s offset:%x read:%llx\n", __func__, UFS_STATUS_INDEX, status);
	*freq = (status & 0x7F) * UNCORE_FREQ_KHZ_MULTIPLIER;

	return 0;
}

static int tpmi_ufs_init(struct auxiliary_device *auxdev)
{
	struct intel_tpmi_plat_info *plat_info;
	struct tpmi_ufs_struct *tpmi_ufs;
	int ret, i, pkg = 0, inst = 0;
	int num_resources;

	num_resources = tpmi_get_resource_count(auxdev);
	dev_dbg(&auxdev->dev, "UFS Number of resources:%x \n", num_resources);
	if (!num_resources)
		return -EINVAL;

	ret = uncore_freq_common_init(uncore_read_control_freq, uncore_write_control_freq, uncore_read_freq);
	if (ret)
		return ret;

	tpmi_ufs = devm_kzalloc(&auxdev->dev, sizeof(*tpmi_ufs), GFP_KERNEL);
	if (!tpmi_ufs) {
		ret = -ENOMEM;
		goto err_rem_common;
	}

	tpmi_ufs->punit_info = devm_kcalloc(&auxdev->dev, num_resources,
					    sizeof(*tpmi_ufs->punit_info),
					    GFP_KERNEL);
	if (!tpmi_ufs->punit_info) {
		ret = -ENOMEM;
		goto err_rem_common;
	}

	tpmi_ufs->number_of_punits = num_resources;

	plat_info = dev_get_platdata(&auxdev->dev);
	if (plat_info)
		pkg = plat_info->package_id;

	for (i = 0; i < num_resources; ++i) {
		struct resource *res;
		u64 cluster_offset;
		u8 cluster_mask;
		int mask, j;
		u64 header;

		dev_dbg(&auxdev->dev, "UFS resources index :%d\n", i);

		res = tpmi_get_resource_at_index(auxdev, i);
		if (!res)
			continue;

		tpmi_ufs->punit_info[i].ufs_base = devm_ioremap_resource(&auxdev->dev, res);
		if (IS_ERR(tpmi_ufs->punit_info[i].ufs_base)) {
			ret = PTR_ERR(tpmi_ufs->punit_info[i].ufs_base);
			tpmi_ufs->punit_info[i].ufs_base = NULL;
			goto err_rem_common;
		}

		header = readq(tpmi_ufs->punit_info[i].ufs_base);
		tpmi_ufs->punit_info[i].ufs_header_ver = header & 0xff;
		if (tpmi_ufs->punit_info[i].ufs_header_ver != UFS_HEADER_VERSION) {
			dev_err(&auxdev->dev, "UFS: Unsupported version:%d\n", tpmi_ufs->punit_info[i].ufs_header_ver);
			continue;
		}

		cluster_mask = (header & 0xff00) >> 8;
		dev_dbg(&auxdev->dev, "UFS version :%d\n", tpmi_ufs->punit_info[i].ufs_header_ver);
		if (tpmi_ufs->punit_info[i].ufs_header_ver != 1)
			continue;

		mask = 0x01;
		for (j = 0; j < 8; ++j) {
			if (cluster_mask & mask)
				tpmi_ufs->punit_info[i].cluster_count++;
			mask <<= 1;
		}
		dev_dbg(&auxdev->dev, "UFS cluster count :%d\n", tpmi_ufs->punit_info[i].cluster_count);
		tpmi_ufs->punit_info[i].cluster_infos = devm_kcalloc(&auxdev->dev, tpmi_ufs->punit_info[i].cluster_count,
									sizeof(struct tpmi_ufs_cluster_info),
									GFP_KERNEL);
		cluster_offset = UFS_FABRIC_CLUSTER_OFFSET;


		cluster_offset = readq((u8 __iomem *)tpmi_ufs->punit_info[i].ufs_base + UFS_FABRIC_CLUSTER_OFFSET);
		cluster_offset *= 8;
		dev_dbg(&auxdev->dev, "UFS cluster offset :%llx\n", cluster_offset);
		for (j = 0; j < tpmi_ufs->punit_info[i].cluster_count && j < 8; ++j) {
			struct tpmi_ufs_cluster_info *cluster_info;

			cluster_info = &tpmi_ufs->punit_info[i].cluster_infos[j];

			cluster_info->cluster_base = (u8 __iomem *)tpmi_ufs->punit_info[i].ufs_base + (cluster_offset & 0xff);

			cluster_info->uncore_data.package_id = pkg;
			cluster_info->uncore_data.die_id = i;
			cluster_info->uncore_data.cluster_id = j;
			cluster_info->auxdev = auxdev;
			ret = uncore_freq_add_entry(&cluster_info->uncore_data, 0);
			if (ret)
				goto err_rem_common;

			cluster_offset >>= 8;
		}
		++inst;
	}

	if (!inst) {
		ret = -ENODEV;
		goto err_rem_common;
	}

	auxiliary_set_drvdata(auxdev, tpmi_ufs);

	pm_runtime_enable(&auxdev->dev);
	pm_runtime_set_autosuspend_delay(&auxdev->dev, 2000);
	pm_runtime_use_autosuspend(&auxdev->dev);
	pm_runtime_put(&auxdev->dev);

	return 0;

err_rem_common:
	uncore_freq_common_exit();

	return ret;
}

static int tpmi_ufs_remove(struct auxiliary_device *auxdev)
{
	struct tpmi_ufs_struct *tpmi_ufs = auxiliary_get_drvdata(auxdev);
	int i;

	for (i = 0; i < tpmi_ufs->number_of_punits; ++i) {
		int j;

		if (!tpmi_ufs->punit_info[i].ufs_base)
			continue;

		for (j = 0; j < tpmi_ufs->punit_info[i].cluster_count && j < 8; ++j) {
			struct tpmi_ufs_cluster_info *cluster_info;

			cluster_info = &tpmi_ufs->punit_info[i].cluster_infos[j];
			uncore_freq_remove_die_entry(&cluster_info->uncore_data);
		}
	}

	pm_runtime_get_sync(&auxdev->dev);
	pm_runtime_put_noidle(&auxdev->dev);
	pm_runtime_disable(&auxdev->dev);

	uncore_freq_common_exit();

	return 0;
}

static int intel_ufs_probe(struct auxiliary_device *auxdev, const struct auxiliary_device_id *id)
{
	return tpmi_ufs_init(auxdev);
}

static void intel_ufs_remove(struct auxiliary_device *auxdev)
{
	tpmi_ufs_remove(auxdev);
}

static const struct auxiliary_device_id intel_ufs_id_table[] = {
	{ .name = "intel_vsec.tpmi-ufs" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, intel_ufs_id_table);

static struct auxiliary_driver intel_ufs_aux_driver = {
	.id_table       = intel_ufs_id_table,
	.remove         = intel_ufs_remove,
	.probe          = intel_ufs_probe,
};

static int __init intel_ufs_init(void)
{
	return auxiliary_driver_register(&intel_ufs_aux_driver);
}
module_init(intel_ufs_init);

static void __exit intel_ufs_exit(void)
{
	auxiliary_driver_unregister(&intel_ufs_aux_driver);
}
module_exit(intel_ufs_exit);

MODULE_IMPORT_NS(INTEL_UNCORE_FREQUENCY);
MODULE_DESCRIPTION("Intel TPMI UFS Driver");
MODULE_LICENSE("GPL");
