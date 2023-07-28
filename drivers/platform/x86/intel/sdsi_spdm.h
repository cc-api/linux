/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DMTF Security Protocol and Data Model (SPDM)
 * https://www.dmtf.org/dsp/DSP0274
 *
 * Copyright (C) 2021-22 Huawei
 *     Jonathan Cameron <Jonathan.Cameron@huawei.com>
 *
 * Copyright (C) 2022-23 Intel Corporation
 */

#ifndef _SDSI_SPDM_H_
#define _SDSI_SPDM_H_

#include <linux/types.h>

struct key;
struct device;
struct sdsi_spdm_state;

enum measurement_op {
	MEASUREMENT_OP_QUERY,
	MEASUREMENT_OP_ONE,
	MEASUREMENT_OP_ALL,
};

#define MEASUREMENT_ATTR_SIGN	BIT(0)
#define MEASUREMENT_ATTR_RAW	BIT(1)

struct spdm_measurement_block {
	u8 index;
	u8 specification;
	u16 size;
	/* Variable measurement size up to 64k */
};

struct spdm_measurement_request {
	enum measurement_op op;
	u8 attribute;
	u8 block_no;
	u8 slot_id;
	void *priv;
	void (*meas_cb)(u8 *measurement, size_t count, void *priv);
};

typedef int (sdsi_spdm_transport)(void *priv, struct device *dev,
			     const void *request, size_t request_sz,
			     void *response, size_t response_sz);

struct sdsi_spdm_state *sdsi_spdm_create(struct device *dev, sdsi_spdm_transport *transport,
			       void *transport_priv, u32 transport_sz,
			       struct key *keyring);

int sdsi_spdm_authenticate(struct sdsi_spdm_state *spdm_state);

int sdsi_spdm_get_measurements(struct sdsi_spdm_state *spdm_state,
			  struct spdm_measurement_request *m);

void sdsi_spdm_destroy(struct sdsi_spdm_state *spdm_state);

#endif
