// SPDX-License-Identifier: GPL-2.0
/*
 * hpm_die_map: Mapping of HPM Die CPU mapping
 *
 * Copyright (c) 2022, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */

#define DEBUG

#include <linux/cpuhotplug.h>
#include <linux/module.h>
#include <linux/topology.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

#include "hpm_die_map.h"

struct hpm_cpu_info {
	u8 punit_thread_id;
	u8 punit_core_id;
	u8 punit_die_id;
	u8 pkg_id;
};

/* The dynamically assigned cpu hotplug state for module_exit() */
static enum cpuhp_state hpm_hp_state __read_mostly;
static DEFINE_PER_CPU(struct hpm_cpu_info, hpm_cpu_info);

#define MAX_PACKAGES	16
#define MAX_DIES	8
static cpumask_t hpm_die_mask[MAX_PACKAGES][MAX_DIES];

static DEFINE_MUTEX(hpm_lock);

static const struct x86_cpu_id hpm_cpu_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(GRANITERAPIDS_X,	NULL),
	X86_MATCH_INTEL_FAM6_MODEL(SIERRAFOREST_X,	NULL),
	{}
};

int hpm_get_linux_cpu_number(int package_id, int die_id, int punit_core_id)
{
	int i;

	/* TBD Optimize this. The usage of this is in slow path, so can wait */
	for (i = 0; i < num_possible_cpus(); ++i) {
		struct hpm_cpu_info *info = &per_cpu(hpm_cpu_info, i);

		if (info->punit_core_id == punit_core_id &&
		    info->punit_die_id == die_id && info->pkg_id == package_id)
		    return i;
	}

	return -1;
}
EXPORT_SYMBOL_GPL(hpm_get_linux_cpu_number);

int hpm_get_punit_core_number(int cpu_no)
{
	struct hpm_cpu_info *info = &per_cpu(hpm_cpu_info, cpu_no);

	if (!info)
		return -EINVAL;

	return info->punit_core_id;
}
EXPORT_SYMBOL_GPL(hpm_get_punit_core_number);

int hpm_get_die_id(int cpu_no)
{
	struct hpm_cpu_info *info = &per_cpu(hpm_cpu_info, cpu_no);

	if (!info)
		return -EINVAL;

	return info->punit_die_id;
}
EXPORT_SYMBOL_GPL(hpm_get_die_id);

cpumask_t *hpm_get_die_mask(int cpu_no)
{
	struct hpm_cpu_info *info = &per_cpu(hpm_cpu_info, cpu_no);
	cpumask_t *mask;

	if (!info || info->pkg_id >= MAX_PACKAGES || info->punit_die_id >= MAX_DIES|| info->punit_die_id < 0)
		return NULL;

	mutex_lock(&hpm_lock);
	mask = &hpm_die_mask[info->pkg_id][info->punit_die_id];
	mutex_unlock(&hpm_lock);

	return mask;
}
EXPORT_SYMBOL_GPL(hpm_get_die_mask);

#define MSR_THREAD_ID_INFO	0x53
#define MSR_PM_LOGICAL_ID	0x54

/*
 * Struct of MSR 0x54
 * [15:11] PM_DOMAIN_ID
 * [10:3] MODULE_ID (aka IDI_AGENT_ID)
 * [2:0] LP_ID
 * For Atom:
 *   [2] Always 0
 *   [1:0] core ID within module
 * For Core
 *   [2:1] Always 0
 *   [0] thread ID
 */
static int hpm_get_logical_id(unsigned int cpu, struct hpm_cpu_info *info)
{
	u64 data;
	int ret;

	ret = rdmsrl_safe(MSR_PM_LOGICAL_ID, &data);
	if (ret) {
		pr_info("MSR MSR_PM_LOGICAL_ID:0x54 is not supported\n");
		return ret;
	}

	/* We don't have use case to differentiate Atom/Core thread id */
	info->punit_thread_id = data & 0x07;
	info->punit_core_id = (data >> 3) & 0xff;
	info->punit_die_id = (data >> 11) & 0x1f;
	info->pkg_id = topology_physical_package_id(cpu);
	pr_debug("using MSR 0x54 cpu:%d core_id:%d die_id:%d pkg_id:%d\n", cpu, info->punit_core_id, info->punit_die_id, info->pkg_id);

	return 0;
}

static int hpm_cpu_online(unsigned int cpu)
{
	struct hpm_cpu_info *info = &per_cpu(hpm_cpu_info, cpu);
	u64 data;
	int ret;

	if (!hpm_get_logical_id(cpu, info))
		goto update_mask;

	ret = rdmsrl_safe(MSR_THREAD_ID_INFO, &data);
	if (ret) {
		info->punit_core_id = -1;
		return 0;
	}

	/*
	 * Format
	 *	Bit 0 – thread ID
	 *	Bit 8:1 – module ID (aka IDI agent ID)
	 *	Bit 13:9 – Compute domain ID (aka die ID)
	 *	Bits 38:32 – co-located CHA ID
	 */
	info->punit_thread_id = data & 0x01;
	info->punit_core_id = (data >> 1) & 0xff;
	info->punit_die_id = (data >> 9) & 0x1f;
	info->pkg_id = topology_physical_package_id(cpu);
	pr_debug("cpu:%d core_id:%d die_id:%d pkg_id:%d\n", cpu, info->punit_core_id, info->punit_die_id, info->pkg_id);

update_mask:
	mutex_lock(&hpm_lock);
	if (info->pkg_id < MAX_PACKAGES && info->punit_die_id < MAX_DIES)
		cpumask_set_cpu(cpu, &hpm_die_mask[info->pkg_id][info->punit_die_id]);
	mutex_unlock(&hpm_lock);

	return 0;
}

static int hpm_cpu_offline(unsigned int cpu)
{
	struct hpm_cpu_info *info = &per_cpu(hpm_cpu_info, cpu);

	mutex_lock(&hpm_lock);
	if (info->pkg_id < MAX_PACKAGES && info->punit_die_id < MAX_DIES)
		cpumask_clear_cpu(cpu, &hpm_die_mask[info->pkg_id][info->punit_die_id]);
	mutex_unlock(&hpm_lock);

	return 0;
}

static int __init hpm_init(void)
{
	const struct x86_cpu_id *id;
	int ret;

	id = x86_match_cpu(hpm_cpu_ids);
	if (!id)
		return -ENODEV;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"platform/x86/hpm_die_map:online",
				hpm_cpu_online,	hpm_cpu_offline);
	if (ret < 0)
		return ret;

	hpm_hp_state = ret;

	return 0;
}
module_init(hpm_init)

static void __exit hpm_exit(void)
{
	cpuhp_remove_state(hpm_hp_state);
}
module_exit(hpm_exit)

MODULE_DESCRIPTION("HPM Die Mapping");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
