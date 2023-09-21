// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI Express Link Bandwidth Notification services driver
 * Author: Alexandru Gagniuc <mr.nuke.me@gmail.com>
 *
 * Copyright (C) 2019, Dell Inc
 *
 * The PCIe Link Bandwidth Notification provides a way to notify the
 * operating system when the link width or data rate changes.  This
 * capability is required for all root ports and downstream ports
 * supporting links wider than x1 and/or multiple link speeds.
 *
 * This service port driver hooks into the bandwidth notification interrupt
 * watching for link speed changes or links becoming degraded in operation
 * and updates the cached link speed exposed to user space.
 */

#define dev_fmt(fmt) "bwctrl: " fmt

#include "../pci.h"
#include "portdrv.h"

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

static int pcie_bandwidth_notification_probe(struct pcie_device *srv)
{
	struct pci_dev *port = srv->port;
	int ret;

	/* Single-width or single-speed ports do not have to support this. */
	if (!pcie_link_bandwidth_notification_supported(port))
		return -ENODEV;

	ret = request_irq(srv->irq, pcie_bw_notification_irq,
			  IRQF_SHARED, "PCIe BW ctrl", srv);
	if (ret)
		return ret;

	pcie_enable_link_bandwidth_notification(port);
	pci_info(port, "enabled with IRQ %d\n", srv->irq);

	return 0;
}

static void pcie_bandwidth_notification_remove(struct pcie_device *srv)
{
	pcie_disable_link_bandwidth_notification(srv->port);
	free_irq(srv->irq, srv);
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
