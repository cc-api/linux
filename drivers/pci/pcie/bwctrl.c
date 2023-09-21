// SPDX-License-Identifier: GPL-2.0+
/*
 * PCIe bandwidth controller
 *
 * Author: Alexandru Gagniuc <mr.nuke.me@gmail.com>
 *
 * Copyright (C) 2019, Dell Inc
 * Copyright (C) 2023 Intel Corporation.
 *
 * The PCIe Bandwidth Controller provides a way to alter PCIe link speeds
 * and notify the operating system when the link width or data rate changes.
 * The notification capability is required for all Root Ports and Downstream
 * Ports supporting links wider than x1 and/or multiple link speeds.
 *
 * This service port driver hooks into the bandwidth notification interrupt
 * watching for link speed changes or links becoming degraded in operation
 * and updates the cached link speed exposed to user space.
 */

#define dev_fmt(fmt) "bwctrl: " fmt

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pci-bwctrl.h>
#include <linux/types.h>

#include <asm/rwonce.h>

#include "../pci.h"
#include "portdrv.h"

/**
 * struct bwctrl_service_data - PCIe Port Bandwidth Controller
 * @set_speed_mutex: serializes link speed changes
 * @cdev: thermal cooling device associated with the port
 */
struct bwctrl_service_data {
	struct mutex set_speed_mutex;
	struct thermal_cooling_device *cdev;
};

static bool bwctrl_valid_pcie_speed(enum pci_bus_speed speed)
{
	return (speed >= PCIE_SPEED_2_5GT) && (speed <= PCIE_SPEED_64_0GT);
}

static u16 speed2lnkctl2(enum pci_bus_speed speed)
{
	static const u16 speed_conv[] = {
		[PCIE_SPEED_2_5GT] = PCI_EXP_LNKCTL2_TLS_2_5GT,
		[PCIE_SPEED_5_0GT] = PCI_EXP_LNKCTL2_TLS_5_0GT,
		[PCIE_SPEED_8_0GT] = PCI_EXP_LNKCTL2_TLS_8_0GT,
		[PCIE_SPEED_16_0GT] = PCI_EXP_LNKCTL2_TLS_16_0GT,
		[PCIE_SPEED_32_0GT] = PCI_EXP_LNKCTL2_TLS_32_0GT,
		[PCIE_SPEED_64_0GT] = PCI_EXP_LNKCTL2_TLS_64_0GT,
	};

	if (WARN_ON_ONCE(!bwctrl_valid_pcie_speed(speed)))
		return 0;

	return speed_conv[speed];
}

static bool pcie_link_bandwidth_notification_supported(struct pci_dev *dev)
{
	int ret;
	u32 lnk_cap;

	ret = pcie_capability_read_dword(dev, PCI_EXP_LNKCAP, &lnk_cap);
	return (ret == PCIBIOS_SUCCESSFUL) && (lnk_cap & PCI_EXP_LNKCAP_LBNC);
}

static void pcie_enable_link_bandwidth_notification(struct pci_dev *dev)
{
	u16 link_status;
	int ret;

	pcie_capability_write_word(dev, PCI_EXP_LNKSTA, PCI_EXP_LNKSTA_LBMS);
	pcie_capability_set_word(dev, PCI_EXP_LNKCTL, PCI_EXP_LNKCTL_LBMIE);

	/* Read after enabling notifications to ensure link speed is up to date */
	ret = pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &link_status);
	if (ret == PCIBIOS_SUCCESSFUL)
		pcie_update_link_speed(dev->subordinate, link_status);
}

static void pcie_disable_link_bandwidth_notification(struct pci_dev *dev)
{
	pcie_capability_clear_word(dev, PCI_EXP_LNKCTL, PCI_EXP_LNKCTL_LBMIE);
}

static irqreturn_t pcie_bw_notification_irq(int irq, void *context)
{
	struct pcie_device *srv = context;
	struct pci_dev *port = srv->port;
	u16 link_status, events;
	int ret;

	ret = pcie_capability_read_word(port, PCI_EXP_LNKSTA, &link_status);
	events = link_status & PCI_EXP_LNKSTA_LBMS;

	if (ret != PCIBIOS_SUCCESSFUL || !events)
		return IRQ_NONE;

	pcie_capability_write_word(port, PCI_EXP_LNKSTA, events);

	/*
	 * The write to clear LBMS prevents getting interrupt from the
	 * latest link speed when the link speed changes between the above
	 * LNKSTA read and write. Therefore, re-read the speed before
	 * updating it.
	 */
	ret = pcie_capability_read_word(port, PCI_EXP_LNKSTA, &link_status);
	if (ret != PCIBIOS_SUCCESSFUL)
		return IRQ_HANDLED;
	pcie_update_link_speed(port->subordinate, link_status);

	return IRQ_HANDLED;
}

/* Configure target speed to the requested speed and set train link */
static int bwctrl_set_speed(struct pci_dev *port, u16 lnkctl2_speed)
{
	int ret;

	ret = pcie_capability_clear_and_set_word(port, PCI_EXP_LNKCTL2,
						 PCI_EXP_LNKCTL2_TLS, lnkctl2_speed);
	if (ret != PCIBIOS_SUCCESSFUL)
		return pcibios_err_to_errno(ret);

	return 0;
}

static int bwctrl_select_speed(struct pcie_device *srv, enum pci_bus_speed *speed)
{
	struct pci_bus *bus = srv->port->subordinate;
	u8 speeds, dev_speeds;
	int i;

	if (*speed > PCIE_LNKCAP2_SLS2SPEED(bus->pcie_bus_speeds))
		return -EINVAL;

	dev_speeds = READ_ONCE(bus->pcie_dev_speeds);
	/* Only the lowest speed can be set when there are no devices */
	if (!dev_speeds)
		dev_speeds = PCI_EXP_LNKCAP2_SLS_2_5GB;

	/*
	 * Implementation Note in PCIe r6.0.1 sec 7.5.3.18 recommends OS to
	 * utilize Supported Link Speeds vector for determining which link
	 * speeds are supported.
	 *
	 * Take into account Supported Link Speeds both from the Root Port
	 * and the device.
	 */
	speeds = bus->pcie_bus_speeds & dev_speeds;
	i = BIT(fls(speeds));
	while (i >= PCI_EXP_LNKCAP2_SLS_2_5GB) {
		enum pci_bus_speed candidate;

		if (speeds & i) {
			candidate = PCIE_LNKCAP2_SLS2SPEED(i);
			if (candidate <= *speed) {
				*speed = candidate;
				return 0;
			}
		}
		i >>= 1;
	}

	return -EINVAL;
}

/**
 * bwctrl_set_current_speed - Set downstream link speed for PCIe port
 * @srv: PCIe port
 * @speed: PCIe bus speed to set
 *
 * Attempts to set PCIe port link speed to @speed. As long as @speed is less
 * than the maximum of what is supported by @srv, the speed is adjusted
 * downwards to the best speed supported by both the port and device
 * underneath it.
 *
 * Return:
 * * 0 - on success
 * * -EINVAL - @speed is higher than the maximum @srv supports
 * * -ETIMEDOUT - changing link speed took too long
 * * -EAGAIN - link speed was changed but @speed was not achieved
 */
int bwctrl_set_current_speed(struct pcie_device *srv, enum pci_bus_speed speed)
{
	struct bwctrl_service_data *data = get_service_data(srv);
	struct pci_dev *port = srv->port;
	u16 link_status;
	int ret;

	if (WARN_ON_ONCE(!bwctrl_valid_pcie_speed(speed)))
		return -EINVAL;

	ret = bwctrl_select_speed(srv, &speed);
	if (ret < 0)
		return ret;

	mutex_lock(&data->set_speed_mutex);
	ret = bwctrl_set_speed(port, speed2lnkctl2(speed));
	if (ret < 0)
		goto unlock;

	ret = pcie_retrain_link(port, true);
	if (ret < 0)
		goto unlock;

	/*
	 * Ensure link speed updates also with platforms that have problems
	 * with notifications
	 */
	ret = pcie_capability_read_word(port, PCI_EXP_LNKSTA, &link_status);
	if (ret == PCIBIOS_SUCCESSFUL)
		pcie_update_link_speed(port->subordinate, link_status);

	if (port->subordinate->cur_bus_speed != speed)
		ret = -EAGAIN;

unlock:
	mutex_unlock(&data->set_speed_mutex);

	return ret;
}

static int pcie_bandwidth_notification_probe(struct pcie_device *srv)
{
	struct bwctrl_service_data *data;
	struct pci_dev *port = srv->port;
	int ret;

	/* Single-width or single-speed ports do not have to support this. */
	if (!pcie_link_bandwidth_notification_supported(port))
		return -ENODEV;

	ret = request_irq(srv->irq, pcie_bw_notification_irq,
			  IRQF_SHARED, "PCIe BW ctrl", srv);
	if (ret)
		return ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto free_irq;
	}
	mutex_init(&data->set_speed_mutex);
	set_service_data(srv, data);

	pcie_enable_link_bandwidth_notification(port);
	pci_info(port, "enabled with IRQ %d\n", srv->irq);

	data->cdev = pcie_cooling_device_register(port, srv);
	if (IS_ERR(data->cdev)) {
		ret = PTR_ERR(data->cdev);
		goto disable_notifications;
	}
	return 0;

disable_notifications:
	pcie_disable_link_bandwidth_notification(srv->port);
	kfree(data);
free_irq:
	free_irq(srv->irq, srv);
	return ret;
}

static void pcie_bandwidth_notification_remove(struct pcie_device *srv)
{
	struct bwctrl_service_data *data = get_service_data(srv);

	pcie_cooling_device_unregister(data->cdev);
	pcie_disable_link_bandwidth_notification(srv->port);
	free_irq(srv->irq, srv);
	mutex_destroy(&data->set_speed_mutex);
	kfree(data);
}

static int pcie_bandwidth_notification_suspend(struct pcie_device *srv)
{
	pcie_disable_link_bandwidth_notification(srv->port);
	return 0;
}

static int pcie_bandwidth_notification_resume(struct pcie_device *srv)
{
	pcie_enable_link_bandwidth_notification(srv->port);
	return 0;
}

static struct pcie_port_service_driver pcie_bandwidth_notification_driver = {
	.name		= "pcie_bwctrl",
	.port_type	= PCIE_ANY_PORT,
	.service	= PCIE_PORT_SERVICE_BWCTRL,
	.probe		= pcie_bandwidth_notification_probe,
	.suspend	= pcie_bandwidth_notification_suspend,
	.resume		= pcie_bandwidth_notification_resume,
	.remove		= pcie_bandwidth_notification_remove,
};

int __init pcie_bwctrl_init(void)
{
	return pcie_port_service_register(&pcie_bandwidth_notification_driver);
}
