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

typedef int (sdsi_spdm_transport)(void *priv, struct device *dev,
			     const void *request, size_t request_sz,
			     void *response, size_t response_sz);

struct sdsi_spdm_state *sdsi_spdm_create(struct device *dev, sdsi_spdm_transport *transport,
			       void *transport_priv, u32 transport_sz,
			       struct key *keyring);

int sdsi_spdm_authenticate(struct sdsi_spdm_state *spdm_state);

void sdsi_spdm_destroy(struct sdsi_spdm_state *spdm_state);

#endif
