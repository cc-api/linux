// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe cooling device
 *
 * Copyright (C) 2023 Intel Corporation.
 */

#include <linux/build_bug.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci-bwctrl.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/thermal.h>

#define COOLING_DEV_TYPE_PREFIX		"PCIe_Port_Link_Speed_"

struct pcie_cooling_device {
	struct pci_dev *port;
	struct pcie_device *pdev;
};

static int pcie_cooling_get_max_level(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct pcie_cooling_device *pcie_cdev = cdev->devdata;

	/* cooling state 0 is same as the maximum PCIe speed */
	*state = pcie_cdev->port->subordinate->max_bus_speed - PCIE_SPEED_2_5GT;

	return 0;
}

static int pcie_cooling_get_cur_level(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct pcie_cooling_device *pcie_cdev = cdev->devdata;

	/* cooling state 0 is same as the maximum PCIe speed */
	*state = cdev->max_state -
		 (pcie_cdev->port->subordinate->cur_bus_speed - PCIE_SPEED_2_5GT);

	return 0;
}

static int pcie_cooling_set_cur_level(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct pcie_cooling_device *pcie_cdev = cdev->devdata;
	enum pci_bus_speed speed;

	/* cooling state 0 is same as the maximum PCIe speed */
	speed = (cdev->max_state - state) + PCIE_SPEED_2_5GT;

	return bwctrl_set_current_speed(pcie_cdev->pdev, speed);
}

static struct thermal_cooling_device_ops pcie_cooling_ops = {
	.get_max_state = pcie_cooling_get_max_level,
	.get_cur_state = pcie_cooling_get_cur_level,
	.set_cur_state = pcie_cooling_set_cur_level,
};

struct thermal_cooling_device *pcie_cooling_device_register(struct pci_dev *port,
							    struct pcie_device *pdev)
{
	struct pcie_cooling_device *pcie_cdev;
	struct thermal_cooling_device *cdev;
	size_t name_len;
	char *name;

	pcie_cdev = kzalloc(sizeof(*pcie_cdev), GFP_KERNEL);
	if (!pcie_cdev)
		return ERR_PTR(-ENOMEM);

	pcie_cdev->port = port;
	pcie_cdev->pdev = pdev;

	name_len = strlen(COOLING_DEV_TYPE_PREFIX) + strlen(pci_name(port)) + 1;
	name = kzalloc(name_len, GFP_KERNEL);
	if (!name) {
		kfree(pcie_cdev);
		return ERR_PTR(-ENOMEM);
	}

	snprintf(name, name_len, COOLING_DEV_TYPE_PREFIX "%s", pci_name(port));
	cdev = thermal_cooling_device_register(name, pcie_cdev, &pcie_cooling_ops);
	kfree(name);

	return cdev;
}

void pcie_cooling_device_unregister(struct thermal_cooling_device *cdev)
{
	struct pcie_cooling_device *pcie_cdev = cdev->devdata;

	thermal_cooling_device_unregister(cdev);
	kfree(pcie_cdev);
}

/* For bus_speed <-> state arithmetic */
static_assert(PCIE_SPEED_2_5GT + 1 == PCIE_SPEED_5_0GT);
static_assert(PCIE_SPEED_5_0GT + 1 == PCIE_SPEED_8_0GT);
static_assert(PCIE_SPEED_8_0GT + 1 == PCIE_SPEED_16_0GT);
static_assert(PCIE_SPEED_16_0GT + 1 == PCIE_SPEED_32_0GT);
static_assert(PCIE_SPEED_32_0GT + 1 == PCIE_SPEED_64_0GT);

MODULE_AUTHOR("Ilpo Järvinen <ilpo.jarvinen@linux.intel.com>");
MODULE_DESCRIPTION("PCIe cooling driver");
