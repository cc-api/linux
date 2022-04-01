/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hpm_die_map: Mapping of HPM Die CPU mapping
 * Copyright (c) 2022, Intel Corporation.
 * All rights reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */

#ifndef _HPM_DIE_MAP_H
#define _HPM_DIE_MAP_H

int hpm_get_linux_cpu_number(int package_id, int die_id, int punit_core_id);
int hpm_get_punit_core_number(int cpu_no);
int hpm_get_die_id(int cpu_no);
cpumask_t *hpm_get_die_mask(int cpu_no);

#endif
