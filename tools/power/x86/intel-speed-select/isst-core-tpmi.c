// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Speed Select -- Enumerate and control features
 * Copyright (c) 2021 Intel Corporation.
 */

#include <linux/isst_if.h>
#include "isst.h"


int tpmi_process_ioctl(int ioctl_no, void *info)
{
	const char *pathname = "/dev/isst_interface";
	int fd;

	if (is_debug_enabled()) {
		debug_printf("Issue IOCTL: ");
		switch(ioctl_no) {
		case ISST_IF_CORE_POWER_STATE:
			debug_printf("ISST_IF_CORE_POWER_STATE\n");
			break;
		case ISST_IF_CLOS_PARAM:
			debug_printf("ISST_IF_CLOS_PARAM\n");
			break;
		case ISST_IF_CLOS_ASSOC:
			debug_printf("ISST_IF_CLOS_ASSOC\n");
			break;
		case ISST_IF_PERF_LEVELS:
			debug_printf("ISST_IF_PERF_LEVELS\n");
			break;
		case ISST_IF_PERF_SET_LEVEL:
			debug_printf("ISST_IF_PERF_SET_LEVEL\n");
			break;
		case ISST_IF_PERF_SET_FEATURE:
			debug_printf("ISST_IF_PERF_SET_FEATURE\n");
			break;
		case ISST_IF_GET_PERF_LEVEL_INFO:
			debug_printf("ISST_IF_GET_PERF_LEVEL_INFO\n");
			break;
		case ISST_IF_GET_PERF_LEVEL_CPU_MASK:
			debug_printf("ISST_IF_GET_PERF_LEVEL_CPU_MASK\n");
			break;
		case ISST_IF_GET_BASE_FREQ_INFO:
			debug_printf("ISST_IF_GET_BASE_FREQ_INFO\n");
			break;
		case ISST_IF_GET_BASE_FREQ_CPU_MASK:
			debug_printf("ISST_IF_GET_BASE_FREQ_CPU_MASK\n");
			break;
		case ISST_IF_GET_TURBO_FREQ_INFO:
			debug_printf("ISST_IF_GET_TURBO_FREQ_INFO\n");
			break;
		case ISST_IF_COUNT_TPMI_INSTANCES:
			debug_printf("ISST_IF_COUNT_TPMI_INSTANCES\n");
			break;
		default:
			debug_printf("%d\n", ioctl_no);
			break;
		}
	}

	fd = open(pathname, O_RDWR);
	if (fd < 0)
		return -1;

	if (ioctl(fd, ioctl_no, info) == -1) {
		debug_printf("IOCTL Failed\n", ioctl);
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

int tpmi_get_instance_count(int pkg, __u16 *valid_mask)
{
	struct isst_tpmi_instance_count info;
	int ret;

	info.socket_id = pkg;
	ret = tpmi_process_ioctl(ISST_IF_COUNT_TPMI_INSTANCES, &info);
	if (ret == -1)
		return 0;

	*valid_mask = info.valid_mask;

	return info.count;
}

int tpmi_isst_set_tdp_level(int cpu, int pkg, int die, int tdp_level)
{
	struct isst_perf_level_control info;
	int ret;

	info.socket_id = pkg;
	info.die_id =  die;
	info.level = tdp_level;

	ret = tpmi_process_ioctl(ISST_IF_PERF_SET_LEVEL, &info);
	if (ret == -1) {
		return ret;
	}

	return 0;
}

int tpmi_isst_get_ctdp_levels(int cpu, int pkg, int die, struct isst_pkg_ctdp *pkg_dev)
{
	struct isst_perf_level_info info;
	int ret;

	info.socket_id = pkg;
	info.die_id = die;

	ret = tpmi_process_ioctl(ISST_IF_PERF_LEVELS, &info);
	if (ret == -1) {
		return ret;
	}

	pkg_dev->version = info.feature_rev;
	pkg_dev->levels = info.levels - 1;
	pkg_dev->locked = info.locked;
	pkg_dev->current_level = info.current_level;
	pkg_dev->locked = info.locked;
	pkg_dev->enabled = info.enabled;

	return 0;
}

int tpmi_isst_get_ctdp_control(int cpu, int pkg, int die, int config_index,
				struct isst_pkg_ctdp_level_info *ctdp_level)
{
	struct isst_core_power core_power_info;
	struct isst_perf_level_info info;
	int ret;


	info.socket_id = pkg;
	info.die_id =  die;

	ret = tpmi_process_ioctl(ISST_IF_PERF_LEVELS, &info);
	if (ret == -1) {
		return -1;
	}

	ctdp_level->fact_support = info.sst_tf_support;
	ctdp_level->pbf_support = info.sst_bf_support;
	ctdp_level->fact_enabled = !!(info.feature_state & BIT(1));
	ctdp_level->pbf_enabled = !!(info.feature_state & BIT(0));

	core_power_info.get_set = 0;
	core_power_info.socket_id = pkg;;
	core_power_info.die_id = die;

	ret = tpmi_process_ioctl(ISST_IF_CORE_POWER_STATE, &core_power_info);
	if (ret == -1) {
		return ret;
	}

	ctdp_level->sst_cp_support = core_power_info.supported;
	ctdp_level->sst_cp_enabled = core_power_info.enable;

	debug_printf(
		"cpu:%d CONFIG_TDP_GET_TDP_CONTROL fact_support:%d pbf_support: %d fact_enabled:%d pbf_enabled:%d\n",
		cpu, ctdp_level->fact_support, ctdp_level->pbf_support,
		ctdp_level->fact_enabled, ctdp_level->pbf_enabled);

	return 0;
}

int tpmi_isst_get_tdp_info(int cpu, int pkg, int die, int config_index,
			    struct isst_pkg_ctdp_level_info *ctdp_level)
{
	struct isst_perf_level_data_info info;
	int ret;

	info.socket_id = pkg;
	info.die_id = die;
	info.level = config_index;

	ret = tpmi_process_ioctl(ISST_IF_GET_PERF_LEVEL_INFO, &info);
	if (ret == -1) {
		return ret;
	}

	ctdp_level->pkg_tdp = info.thermal_design_power_w;
	ctdp_level->tdp_ratio = info.tdp_ratio;
	ctdp_level->sse_p1 = info.base_freq_mhz;
	ctdp_level->avx2_p1 = info.base_freq_avx2_mhz;
	ctdp_level->avx512_p1 = info.base_freq_avx512_mhz;
	ctdp_level->amx_p1 = info.base_freq_amx_mhz;

	ctdp_level->t_proc_hot = info.tjunction_max_c;
	ctdp_level->mem_freq = info.max_memory_freq_mhz;
	ctdp_level->cooling_type = info.cooling_type;

	ctdp_level->uncore_p0 = info.p0_fabric_ratio * 100;
	ctdp_level->uncore_p1 = info.p1_fabric_ratio * 100;

	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_TDP_INFO tdp_ratio:%d pkg_tdp:%d ctdp_level->t_proc_hot:%d\n",
		cpu, config_index, ctdp_level->tdp_ratio,
		ctdp_level->pkg_tdp, ctdp_level->t_proc_hot);

	return 0;
}

int tpmi_isst_get_trl_bucket_info(int cpu, int pkg, int die, int config_index, unsigned long long *buckets_info)
{
	struct isst_perf_level_data_info info;
	unsigned char *mask = (unsigned char *) buckets_info;
	int ret;

	info.socket_id =  pkg;
	info.die_id = die;
	info.level = config_index;

	ret = tpmi_process_ioctl(ISST_IF_GET_PERF_LEVEL_INFO, &info);
	if (ret == -1) {
		return ret;
	}

	mask[0] = info.bucket0_core_count;
	mask[1] = info.bucket1_core_count;
	mask[2] = info.bucket2_core_count;
	mask[3] = info.bucket3_core_count;
	mask[4] = info.bucket4_core_count;
	mask[5] = info.bucket5_core_count;
	mask[6] = info.bucket6_core_count;
	mask[7] = info.bucket7_core_count;

	debug_printf("cpu:%d TRL bucket info: 0x%llx\n", cpu,
		     *buckets_info);

	return 0;
}

int tpmi_isst_get_get_trl(int cpu, int pkg, int die, int config_index, struct isst_pkg_ctdp_level_info *ctdp_level)
{
	struct isst_perf_level_data_info info;
	int ret;

	info.socket_id = pkg;
	info.die_id = die;
	info.level = config_index;

	ret = tpmi_process_ioctl(ISST_IF_GET_PERF_LEVEL_INFO, &info);
	if (ret == -1) {
		return ret;
	}

	ctdp_level->trl_cdyn_level[0][0] = info.cdyn0_bucket0_freq_mhz;
	ctdp_level->trl_cdyn_level[0][1] = info.cdyn0_bucket1_freq_mhz;
	ctdp_level->trl_cdyn_level[0][2] = info.cdyn0_bucket2_freq_mhz;
	ctdp_level->trl_cdyn_level[0][3] = info.cdyn0_bucket3_freq_mhz;
	ctdp_level->trl_cdyn_level[0][4] = info.cdyn0_bucket4_freq_mhz;
	ctdp_level->trl_cdyn_level[0][5] = info.cdyn0_bucket5_freq_mhz;
	ctdp_level->trl_cdyn_level[0][6] = info.cdyn0_bucket6_freq_mhz;
	ctdp_level->trl_cdyn_level[0][7] = info.cdyn0_bucket7_freq_mhz;

	ctdp_level->trl_cdyn_level[1][0] = info.cdyn1_bucket0_freq_mhz;
	ctdp_level->trl_cdyn_level[1][1] = info.cdyn1_bucket1_freq_mhz;
	ctdp_level->trl_cdyn_level[1][2] = info.cdyn1_bucket2_freq_mhz;
	ctdp_level->trl_cdyn_level[1][3] = info.cdyn1_bucket3_freq_mhz;
	ctdp_level->trl_cdyn_level[1][4] = info.cdyn1_bucket4_freq_mhz;
	ctdp_level->trl_cdyn_level[1][5] = info.cdyn1_bucket5_freq_mhz;
	ctdp_level->trl_cdyn_level[1][6] = info.cdyn1_bucket6_freq_mhz;
	ctdp_level->trl_cdyn_level[1][7] = info.cdyn1_bucket7_freq_mhz;

	ctdp_level->trl_cdyn_level[2][0] = info.cdyn2_bucket0_freq_mhz;
	ctdp_level->trl_cdyn_level[2][1] = info.cdyn2_bucket1_freq_mhz;
	ctdp_level->trl_cdyn_level[2][2] = info.cdyn2_bucket2_freq_mhz;
	ctdp_level->trl_cdyn_level[2][3] = info.cdyn2_bucket3_freq_mhz;
	ctdp_level->trl_cdyn_level[2][4] = info.cdyn2_bucket4_freq_mhz;
	ctdp_level->trl_cdyn_level[2][5] = info.cdyn2_bucket5_freq_mhz;
	ctdp_level->trl_cdyn_level[2][6] = info.cdyn2_bucket6_freq_mhz;
	ctdp_level->trl_cdyn_level[2][7] = info.cdyn2_bucket7_freq_mhz;

	ctdp_level->trl_cdyn_level[3][0] = info.cdyn3_bucket0_freq_mhz;
	ctdp_level->trl_cdyn_level[3][1] = info.cdyn3_bucket1_freq_mhz;
	ctdp_level->trl_cdyn_level[3][2] = info.cdyn3_bucket2_freq_mhz;
	ctdp_level->trl_cdyn_level[3][3] = info.cdyn3_bucket3_freq_mhz;
	ctdp_level->trl_cdyn_level[3][4] = info.cdyn3_bucket4_freq_mhz;
	ctdp_level->trl_cdyn_level[3][5] = info.cdyn3_bucket5_freq_mhz;
	ctdp_level->trl_cdyn_level[3][6] = info.cdyn3_bucket6_freq_mhz;
	ctdp_level->trl_cdyn_level[3][7] = info.cdyn3_bucket7_freq_mhz;

	return 0;
}

int tpmi_isst_get_pwr_info(int cpu, int pkg, int die, int config_index,
			    struct isst_pkg_ctdp_level_info *ctdp_level)
{
	/* TBD */
	ctdp_level->pkg_max_power = 0;
	ctdp_level->pkg_min_power = 0;

	debug_printf(
		"cpu:%d ctdp:%d CONFIG_TDP_GET_PWR_INFO pkg_max_power:%d pkg_min_power:%d\n",
		cpu, config_index, ctdp_level->pkg_max_power,
		ctdp_level->pkg_min_power);

	return 0;
}

int tpmi_isst_get_coremask_info(int cpu, int pkg, int die, int config_index,
				 struct isst_pkg_ctdp_level_info *ctdp_level)
{
	struct isst_perf_level_cpu_mask info;
	int i, ret, cpu_count;

	info.socket_id =  pkg;
	info.die_id = die;
	info.level = config_index;

	ret = tpmi_process_ioctl(ISST_IF_GET_PERF_LEVEL_CPU_MASK, &info);
	if (ret == -1)
		return ret;

	if (info.punit_cpu_map) {
		set_cpu_mask_from_punit_coremask(cpu, info.mask,
						 ctdp_level->core_cpumask_size,
						 ctdp_level->core_cpumask,
						 &cpu_count);
		ctdp_level->cpu_count = cpu_count;
	} else {
		for (i = 0; i < info.cpu_count; ++i) {
			CPU_SET_S(info.cpus[i], ctdp_level->core_cpumask_size, ctdp_level->core_cpumask);
		}
		ctdp_level->cpu_count = info.cpu_count;
	}

	debug_printf(
		"cpu:%d ctdp:%d core_mask ino cpu count:%d \n",
		cpu, config_index, ctdp_level->cpu_count);

	return 0;
}

int tpmi_isst_pbf_get_coremask_info(int cpu, int pkg, int die, int config_index,
				     struct isst_pbf_info *pbf_info)
{
	struct isst_perf_level_cpu_mask info;
	int i, ret, cpu_count;

	info.socket_id =  pkg;
	info.die_id = die;
	info.level = config_index;

	ret = tpmi_process_ioctl(ISST_IF_GET_BASE_FREQ_CPU_MASK, &info);
	if (ret == -1)
		return ret;

	if (info.punit_cpu_map) {
		set_cpu_mask_from_punit_coremask(cpu, info.mask,
						 pbf_info->core_cpumask_size,
						 pbf_info->core_cpumask,
						 &cpu_count);
	} else {
		for (i = 0; i < info.cpu_count; ++i) {
			CPU_SET_S(info.cpus[i], pbf_info->core_cpumask_size, pbf_info->core_cpumask);
		}
	}
	debug_printf(
		"cpu:%d ctdp:%d pbf core_mask ino cpu count:%d \n",
		cpu, config_index, info.cpu_count);

	return 0;
}

int tpmi_isst_get_pbf_info(int cpu, int pkg, int die, int level, struct isst_pbf_info *pbf_info)
{
	struct isst_base_freq_info info;
	int ret;

	info.socket_id =  pkg;
	info.die_id = die;
	info.level = level;

	ret = tpmi_process_ioctl(ISST_IF_GET_BASE_FREQ_INFO, &info);
	if (ret == -1)
		return ret;

	pbf_info->p1_low = info.low_base_freq_mhz;
	pbf_info->p1_high = info.high_base_freq_mhz;
	pbf_info->tdp = info.thermal_design_power_w;
	pbf_info->t_prochot = info.tjunction_max_c;

	debug_printf(
		"cpu:%d ctdp:%d pbf info:%d:%d:%d:%d\n",
		cpu, level, pbf_info->p1_low, pbf_info->p1_high, pbf_info->tdp, pbf_info->t_prochot);

	return tpmi_isst_pbf_get_coremask_info(cpu, pkg, die, level, pbf_info);
}

int tpmi_isst_set_pbf_fact_status(int cpu, int pkg, int die, int pbf, int fact, int enable)
{
	struct isst_perf_feature_control info;
	int ret;

	info.socket_id = pkg;
	info.die_id = die;

	info.feature = 0;
	if (pbf)
		info.feature = 0x01;

	if (fact)
		info.feature |= 0x02;

	ret = tpmi_process_ioctl(ISST_IF_PERF_SET_FEATURE, &info);
	if (ret == -1) {
		return ret;
	}

	return 0;
}

int tpmi_get_fact_info(int cpu, int pkg, int die, int level, int fact_bucket, struct isst_fact_info *fact_info)
{
	struct isst_turbo_freq_info info;

	int ret;

	info.socket_id = get_physical_package_id(cpu);
	info.die_id =  get_physical_die_id(cpu);

	ret = tpmi_process_ioctl(ISST_IF_GET_TURBO_FREQ_INFO, &info);
	if (ret == -1) {
		return ret;
	}


	fact_info->lp_clipping_ratio_license_sse = info.lp_clip_0_mhz;
	fact_info->lp_clipping_ratio_license_avx2 = info.lp_clip_1_mhz;
	fact_info->lp_clipping_ratio_license_avx512 = info.lp_clip_2_mhz;

	fact_info->bucket_info[0].sse_trl = info.bucket_0_cydn_level_0_trl;
	fact_info->bucket_info[0].avx_trl = info.bucket_0_cydn_level_1_trl;
	fact_info->bucket_info[0].avx512_trl = info.bucket_0_cydn_level_2_trl;
	fact_info->bucket_info[0].high_priority_cores_count = info.bucket_0_core_count;

	fact_info->bucket_info[1].sse_trl = info.bucket_1_cydn_level_0_trl;
	fact_info->bucket_info[1].avx_trl = info.bucket_1_cydn_level_1_trl;
	fact_info->bucket_info[1].avx512_trl = info.bucket_1_cydn_level_2_trl;
	fact_info->bucket_info[1].high_priority_cores_count = info.bucket_1_core_count;

	fact_info->bucket_info[2].sse_trl = info.bucket_2_cydn_level_0_trl;
	fact_info->bucket_info[2].avx_trl = info.bucket_2_cydn_level_1_trl;
	fact_info->bucket_info[2].avx512_trl = info.bucket_2_cydn_level_2_trl;
	fact_info->bucket_info[2].high_priority_cores_count = info.bucket_2_core_count;

	return 0;
}

void tpmi_isst_get_uncore_p0_p1_info(int cpu, int pkg, int die, int config_index,
									 struct isst_pkg_ctdp_level_info *ctdp_level)
{
	/* Not required. Data is already collected in tpmi_isst_get_tdp_info() */

}

void tpmi_isst_get_p1_info(int cpu, int pkg, int die, int config_index,
						   struct isst_pkg_ctdp_level_info *ctdp_level)
{
	/* Not required. Data is already collected in tpmi_isst_get_tdp_info() */
}

void tpmi_isst_get_uncore_mem_freq(int cpu, int pkg, int die, int config_index,
								   struct isst_pkg_ctdp_level_info *ctdp_level)
{
	/* Not required. Data is already collected in tpmi_isst_get_tdp_info() */

}

int tpmi_isst_read_pm_config(int cpu, int pkg, int die, int *cp_state, int *cp_cap)
{
	struct isst_core_power info;
	int ret;

	info.get_set = 0;
	info.socket_id =  get_physical_package_id(cpu);;
	info.die_id = get_physical_die_id(cpu);
	ret = tpmi_process_ioctl(ISST_IF_CORE_POWER_STATE, &info);
	if (ret == -1)
		return ret;

	*cp_state = info.enable;
	*cp_cap = info.supported;

	return 0;
}

int tpmi_isst_clos_get_clos_information(int cpu, int pkg, int die, int *enable, int *type)
{
	struct isst_core_power info;
	int ret;

	info.get_set = 0;
	info.socket_id =  pkg;
	info.die_id = die;
	ret = tpmi_process_ioctl(ISST_IF_CORE_POWER_STATE, &info);
	if (ret == -1)
		return ret;

	*enable = info.enable;
	*type = info.priority_type;

	return 0;
}

int tpmi_isst_pm_get_clos(int cpu, int clos, struct isst_clos_config *clos_config)
{
	struct isst_clos_param info;
	int ret;

	info.get_set = 0;
	info.socket_id =  clos_config->pkg_id;
	info.die_id = clos_config->die_id;
	info.clos = clos;
	ret = tpmi_process_ioctl(ISST_IF_CLOS_PARAM, &info);
	if (ret == -1)
		return ret;

	clos_config->epp = 0;
	clos_config->clos_prop_prio = info.prop_prio;
	clos_config->clos_min = info.min_freq_mhz;
	clos_config->clos_max = info.max_freq_mhz;
	clos_config->clos_desired = 0;

	debug_printf("cpu:%d clos:%d min:%d max:%d\n", cpu, clos, clos_config->clos_min, clos_config->clos_max);

	return 0;
}

int tpmi_isst_set_clos(int cpu, int clos, struct isst_clos_config *clos_config)
{
	struct isst_clos_param info;
	int ret;

	info.get_set = 1;
	info.socket_id = clos_config->pkg_id;
	info.die_id = clos_config->die_id;
	info.clos = clos;
	info.prop_prio = clos_config->clos_prop_prio;
	info.min_freq_mhz = clos_config->clos_min;
	info.max_freq_mhz = clos_config->clos_max;
	ret = tpmi_process_ioctl(ISST_IF_CLOS_PARAM, &info);
	if (ret == -1)
		return ret;

	debug_printf("set cpu:%d clos:%d min:%d max:%d\n", cpu, clos, clos_config->clos_min, clos_config->clos_max);

	return 0;
}

int tpmi_isst_pm_qos_config(int cpu, int pkg, int die, int enable_clos, int priority_type)
{
	struct isst_core_power info;
	int ret;

	info.get_set = 1;
	info.socket_id = pkg;
	info.die_id = die;
	info.enable = enable_clos;
	info.priority_type = priority_type;
	ret = tpmi_process_ioctl(ISST_IF_CORE_POWER_STATE, &info);
	if (ret == -1)
		return ret;

	return 0;
}

int tpmi_isst_clos_associate(int cpu, int pkg, int die, int clos_id)
{
	struct isst_if_clos_assoc_cmds assoc_cmds;
	int ret;

	assoc_cmds.cmd_count = 1;
	assoc_cmds.get_set = 1;
	assoc_cmds.punit_cpu_map = 1;
	assoc_cmds.assoc_info[0].logical_cpu = find_phy_core_num(cpu);
	assoc_cmds.assoc_info[0].clos = clos_id;
	assoc_cmds.assoc_info[0].socket_id = pkg;
	assoc_cmds.assoc_info[0].die_id = die;

	ret = tpmi_process_ioctl(ISST_IF_CLOS_ASSOC, &assoc_cmds);
	if (ret == -1)
		return ret;

	return 0;
}

int tpmi_isst_clos_get_assoc_status(int cpu, int pkg, int die, int *clos_id)
{
	struct isst_if_clos_assoc_cmds assoc_cmds;
	int ret;

	assoc_cmds.cmd_count = 1;
	assoc_cmds.get_set = 0;
	assoc_cmds.punit_cpu_map = 1;
	assoc_cmds.assoc_info[0].logical_cpu = find_phy_core_num(cpu);
	assoc_cmds.assoc_info[0].socket_id = pkg;
	assoc_cmds.assoc_info[0].die_id = die;

	ret = tpmi_process_ioctl(ISST_IF_CLOS_ASSOC, &assoc_cmds);
	if (ret == -1)
		return ret;

	*clos_id = assoc_cmds.assoc_info[0].clos;

	return 0;
}
