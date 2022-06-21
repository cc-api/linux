/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SDSI_H_
#define __SDSI_H_

#include <linux/auxiliary_bus.h>

#define SDSI_SPDM_MAX_TRANSPORT		4096

struct sdsi_spdm_state;

struct sdsi_priv {
	struct mutex			mb_lock;	/* Mailbox access lock */
	struct device			*dev;
	struct intel_vsec_device	*ivdev;
	struct sdsi_spdm_state		*spdm_state;
	struct key			*keyring;
	struct list_head		node;
	void __iomem			*control_addr;
	void __iomem			*mbox_addr;
	void __iomem			*regs_addr;
	const char			*name;
	int				id;
	int				control_size;
	int				maibox_size;
	int				registers_size;
	u32				guid;
	u32				features;
};

int sdsi_spdm_exchange(void *private, struct device *dev, const void *request,
		       size_t request_sz, void *response, size_t response_sz);
int for_each_sdsi_device(int (*cb)(struct sdsi_priv *, void *),
			 void *data);
struct sdsi_priv *sdsi_dev_get_by_id(int id);
#endif
