// SPDX-License-Identifier: GPL-2.0-only
/*
 * processor thermal device for registering and processing
 * power floor. When the hardware reduces the power to the
 * minimum possible, the power floor is notified via an
 * interrupt.
 *
 * Operation:
 * When user space enables power floor enable:
 *	Enable processor thermal device interrupt via mail box
 * - The power floor status is read from MMIO:
 *		Offset 0x5B18 shows if there was an interrupt
 *		active for change in power floor log
 *
 * Two interface function are provided to call when there is a
 * thermal device interrupt:
 * - proc_thermal_power_floor_intr(): Check if the is interrupt for
 *	power floor
 * - proc_thermal_power_floor_callback(): Callback for interrupt
 * under thread context to process. This involves sending
 * notification to user space that there is an active power floor
 * status
 *
 * Copyright (c) 2020-2023, Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/pci.h>
#include "processor_thermal_device.h"

#define SOC_POWER_FLOOR_INT_STATUS_OFF	0x5B18
#define SOC_POWER_FLOOR_STATUS			BIT(39)
#define SOC_POWER_FLOOR_SHIFT			39

#define SOC_POWER_FLOOR_INT_ENABLE_BIT	31

#define SOC_POWER_FLOOR_INT_ACTIVE	BIT(3)

/* Mark time windows as valid as this is not applicable */
#define SOC_POWER_FLOOR_TIME_WINDOW	-1

/*
 * Callback to check if interrupt for prediction is active.
 * Caution: Called from interrupt context.
 */
bool proc_thermal_check_power_floor_intr(struct proc_thermal_device *proc_priv)
{
	u64 int_status;

	int_status = readq(proc_priv->mmio_base + SOC_POWER_FLOOR_INT_STATUS_OFF);
	if (int_status & SOC_POWER_FLOOR_INT_ACTIVE)
		return true;

	return false;
}
EXPORT_SYMBOL_NS_GPL(proc_thermal_check_power_floor_intr, INT340X_THERMAL);

/* Callback to notify user space */
void proc_thermal_power_floor_intr_callback(struct pci_dev *pdev, struct proc_thermal_device *proc_priv)
{
	u64 status;

	status = readq(proc_priv->mmio_base + SOC_POWER_FLOOR_INT_STATUS_OFF);
	if (status & SOC_POWER_FLOOR_INT_ACTIVE) {
		writeq(status & ~SOC_POWER_FLOOR_INT_ACTIVE,
			   proc_priv->mmio_base + SOC_POWER_FLOOR_INT_STATUS_OFF);
		sysfs_notify(&pdev->dev.kobj, "power_limits", "power_floor_status");

	}
}
EXPORT_SYMBOL_NS_GPL(proc_thermal_power_floor_intr_callback, INT340X_THERMAL);

int proc_thermal_read_power_floor_status(struct proc_thermal_device *proc_priv)
{
	u64 status = 0;

	status = readq(proc_priv->mmio_base + SOC_POWER_FLOOR_INT_STATUS_OFF);
	return (status & SOC_POWER_FLOOR_STATUS) >> SOC_POWER_FLOOR_SHIFT;
}
EXPORT_SYMBOL_NS_GPL(proc_thermal_read_power_floor_status, INT340X_THERMAL);

int proc_thermal_power_floor_add(struct pci_dev *pdev, struct proc_thermal_device *proc_priv)
{
	int ret;

	ret = processor_thermal_mbox_interrupt_config(pdev, true,
												  SOC_POWER_FLOOR_INT_ENABLE_BIT,
												  SOC_POWER_FLOOR_TIME_WINDOW);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(proc_thermal_power_floor_add, INT340X_THERMAL);

void proc_thermal_power_floor_remove(struct pci_dev *pdev)
{
	processor_thermal_mbox_interrupt_config(pdev, false,
											SOC_POWER_FLOOR_INT_ENABLE_BIT,
											SOC_POWER_FLOOR_TIME_WINDOW);
}
EXPORT_SYMBOL_NS_GPL(proc_thermal_power_floor_remove, INT340X_THERMAL);

MODULE_IMPORT_NS(INT340X_THERMAL);
MODULE_LICENSE("GPL");
