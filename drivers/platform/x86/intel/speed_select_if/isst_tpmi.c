// SPDX-License-Identifier: GPL-2.0
/*
 * intel-tpmi-sst: SST TPMI interface
 *
 * Copyright (c) 2021, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */
#define DEBUG
#include <linux/auxiliary_bus.h>
#include <linux/module.h>
#include <linux/intel_tpmi.h>

#include "isst_tpmi_core.h"

static int intel_sst_probe(struct auxiliary_device *auxdev, const struct auxiliary_device_id *id)
{
	int ret;

	ret = tpmi_sst_init();
	if (ret)
		return ret;

	ret = tpmi_sst_dev_add(auxdev);
	if (ret)
		tpmi_sst_exit();

	return ret;
}

static void intel_sst_remove(struct auxiliary_device *auxdev)
{
	tpmi_sst_dev_remove(auxdev);
	tpmi_sst_exit();
}

static const struct auxiliary_device_id intel_sst_id_table[] = {
	{ .name = "intel_vsec.tpmi-sst" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, intel_sst_id_table);

static struct auxiliary_driver intel_sst_aux_driver = {
	.id_table       = intel_sst_id_table,
	.remove         = intel_sst_remove,
	.probe          = intel_sst_probe,
};

static int __init intel_sst_init(void)
{
	return auxiliary_driver_register(&intel_sst_aux_driver);
}
module_init(intel_sst_init);

static void __exit intel_sst_exit(void)
{
	auxiliary_driver_unregister(&intel_sst_aux_driver);
}
module_exit(intel_sst_exit);

MODULE_DESCRIPTION("Intel TPMI SST Driver");
MODULE_LICENSE("GPL v2");
