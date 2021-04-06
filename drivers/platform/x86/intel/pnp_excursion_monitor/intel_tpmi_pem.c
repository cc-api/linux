// SPDX-License-Identifier: GPL-2.0
/*
 * intel-pem-tpmi: platform excursion monitor enabling
 *
 * Copyright (c) 2021, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include <linux/auxiliary_bus.h>
#include <linux/intel_tpmi.h>
#include <linux/module.h>

#include "intel_tpmi_pem_core.h"

static int intel_pem_probe(struct auxiliary_device *auxdev, const struct auxiliary_device_id *id)
{
	int ret;

	ret = tpmi_pem_init();
	if (ret)
		return ret;

	ret = tpmi_pem_dev_add(auxdev);
	if (ret)
		tpmi_pem_exit();

	return ret;
}

static void intel_pem_remove(struct auxiliary_device *auxdev)
{
	tpmi_pem_dev_remove(auxdev);
	tpmi_pem_exit();
}

static const struct auxiliary_device_id intel_pem_id_table[] = {
	{ .name = "intel_vsec.tpmi-pem" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, intel_pem_id_table);

static struct auxiliary_driver intel_pem_aux_driver = {
	.id_table       = intel_pem_id_table,
	.remove         = intel_pem_remove,
	.probe          = intel_pem_probe,
};

static int __init intel_pem_init(void)
{
	return auxiliary_driver_register(&intel_pem_aux_driver);
}
module_init(intel_pem_init);

static void __exit intel_pem_exit(void)
{
	auxiliary_driver_unregister(&intel_pem_aux_driver);
}
module_exit(intel_pem_exit);

MODULE_DESCRIPTION("Intel TPMI PEM Driver");
MODULE_LICENSE("GPL v2");
