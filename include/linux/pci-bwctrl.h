/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCIe bandwidth controller
 *
 * Copyright (C) 2023 Intel Corporation.
 */

#ifndef LINUX_PCI_BWCTRL_H
#define LINUX_PCI_BWCTRL_H

#include <linux/pci.h>

struct pcie_device;
struct thermal_cooling_device;

int bwctrl_set_current_speed(struct pcie_device *srv, enum pci_bus_speed speed);

#ifdef CONFIG_PCIE_THERMAL
struct thermal_cooling_device *pcie_cooling_device_register(struct pci_dev *port,
							    struct pcie_device *pdev);
void pcie_cooling_device_unregister(struct thermal_cooling_device *cdev);
#else
static inline struct thermal_cooling_device *pcie_cooling_device_register(struct pci_dev *port,
									  struct pcie_device *pdev)
{
	return NULL;
}
static inline void pcie_cooling_device_unregister(struct thermal_cooling_device *cdev)
{
}
#endif

#endif
