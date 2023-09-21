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

int bwctrl_set_current_speed(struct pcie_device *srv, enum pci_bus_speed speed);

#endif
