/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel PEM Interface: Drivers Internal interfaces
 * Copyright (c) 2021, Intel Corporation.
 * All rights reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */

#ifndef _INTEL_TPMI_PEM_CORE_H
#define _INTEL_TPMI_PEM_CORE_H

int tpmi_pem_init(void);
void tpmi_pem_exit(void);
int tpmi_pem_dev_add(struct auxiliary_device *auxdev);
void tpmi_pem_dev_remove(struct auxiliary_device *auxdev);

#endif
