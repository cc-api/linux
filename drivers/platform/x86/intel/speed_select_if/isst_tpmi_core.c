// SPDX-License-Identifier: GPL-2.0
/*
 * intel-tpmi-sst: SST TPMI core
 *
 * Copyright (c) 2021, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */
#define DEBUG
#include <linux/auxiliary_bus.h>
#include <linux/intel_tpmi.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <uapi/linux/isst_if.h>

#include "isst_tpmi_core.h"
#include "isst_if_common.h"

#define TPMI_ISST_IF_API_VERSION	0x02 /* TPMI Based */
#define TPMI_ISST_IF_DRIVER_VERSION	0x01
#define TPMI_ISST_IF_CMD_LIMIT	64

#define ISST_HEADER_VERSION	1

struct cp_header {
	u64 feature_id :4;
	u64 feature_rev :8;
	u64 ratio_unit :2;
	u64 resd :50;
};

struct pp_header {
	u64 feature_id :4;
	u64 feature_rev :8;
	u64 level_en_mask :8;
	u64 allowed_level_mask :8;
	u64 num_avx_levels :3;
	u64 resd0 :1;
	u64 ratio_unit :2;
	u64 block_size :8;
	u64 resd :22;
};

struct feature_offset {
	u64 pp_offset :8;
	u64 bf_offset :8;
	u64 tf_offset :8;
	u64 resd :40;
};

struct levels_offset {
	u64 sst_pp_level0_offset :8;
	u64 sst_pp_level1_offset :8;
	u64 sst_pp_level2_offset :8;
	u64 sst_pp_level3_offset :8;
	u64 sst_pp_level4_offset :8;
	u64 resd :24;
};

struct pp_control_offset {
	u64 perf_level :3;
	u64 perf_level_lock :1;
	u64 resvd :4;
	u64 current_state :8;
	u64 resd :48;
};

struct pp_status_offset {
	u64 sst_pp_level :3;
	u64 sst_pp_lock :1;
	u64 error_type :4;
	u64 feature_state :8;
	u64 resd0 :16;
	u64 feature_error_type;
	u64 resd1 :8;
};

struct sst_header {
	u8 interface_version;
	u8 cap_mask;
	u8 cp_offset;
	u8 pp_offset;
};

struct perf_level {
	int mmio_offset;
	int level;
};

struct tpmi_per_punit_info {
	int package_id;
	int die_id;
	int level_count;
	int ratio_unit;
	int avx_levels;
	int pp_block_size;
	struct sst_header sst_header;
	struct cp_header cp_header;
	struct pp_header pp_header;
	struct perf_level *perf_levels;
	struct feature_offset feature_offsets;
	struct pp_control_offset control_offset;
	struct pp_status_offset status_offset;
	void __iomem *sst_base;
	struct auxiliary_device *auxdev;
};

struct tpmi_sst_struct {
	int pkg_id;
	int number_of_punits;
	struct tpmi_per_punit_info *punit_info;
};

#define	SST_MAX_INSTANCES	16
struct tpmi_sst_common_struct {
	int online_id;
	struct tpmi_sst_struct *sst_inst[SST_MAX_INSTANCES];
};

struct sst_cpu_info {
	int punit_cpu_id;
	int apic_die_id;
	int die_id;
	int pkg_id;
};

static DEFINE_MUTEX(iss_tpmi_dev_lock);
static int isst_core_usage_count;
struct tpmi_sst_common_struct sst_common;

static struct tpmi_per_punit_info *get_instance(int pkg_id, int die_id)
{
	struct tpmi_per_punit_info *punit_info = NULL;
	struct tpmi_sst_struct *sst_inst;
	int i;

	pr_debug("%s pkg:%d die:%d\n", __func__, pkg_id, die_id);

	if (pkg_id < 0 || die_id < 0 || pkg_id >= SST_MAX_INSTANCES)
		return NULL;

	sst_inst = sst_common.sst_inst[pkg_id];
	if (!sst_inst)
		return NULL;

	/* Die id has holes, so we have to match in the list */
	for (i = 0; i < sst_inst->number_of_punits; ++i) {
		if (sst_inst->punit_info[i].die_id == die_id) {
			punit_info = &sst_inst->punit_info[i];
			break;
		}
	}

	if (!punit_info)
		return NULL;

	if (!punit_info->sst_base)
		return NULL;

	return punit_info;
}

#define _read_cp_info(name_str, name, offset, shift, mask, mult_factor)\
{\
	u64 val;\
	\
	val = intel_tpmi_readq(punit_info->auxdev, punit_info->sst_base + punit_info->sst_header.cp_offset + (offset));\
	val >>= shift;\
	val &= mask;\
	name = (val * mult_factor);\
	pr_debug("cp_info %s var:%s cp_offset:%x offset:%x shift:%x mask:%x mul_factor:%x res:%x\n",\
		__func__, name_str, punit_info->sst_header.cp_offset, offset, shift, mask, mult_factor, name);\
}

#define _write_cp_info(name_str, name, offset, shift, bits, mask, div_factor)\
{\
	u64 val, _mask;\
	\
	val = intel_tpmi_readq(punit_info->auxdev, punit_info->sst_base + punit_info->sst_header.cp_offset + (offset));\
	_mask = GENMASK((shift + bits - 1), shift);\
	val &= ~_mask;\
	val |= (name / div_factor) << shift;\
	intel_tpmi_writeq(punit_info->auxdev, val, punit_info->sst_base + punit_info->sst_header.cp_offset + (offset));\
	pr_debug("wr_cp_info %s var:%s wr:%x cp_offset:%x offset:%x shift:%x mask:%x div_factor:%x res:%llx\n",\
		__func__, name_str, name, punit_info->sst_header.cp_offset, offset, shift, mask, div_factor, val);\
}

static long isst_if_core_power_state(void __user *argp)
{
	struct tpmi_per_punit_info *punit_info;
	struct isst_core_power core_power;

	if (copy_from_user(&core_power, argp, sizeof(core_power)))
		return -EFAULT;

	punit_info = get_instance(core_power.socket_id, core_power.die_id);
	if (!punit_info)
		return -EINVAL;

	if (core_power.get_set) {
		_write_cp_info("cp_enable", core_power.enable, 8, 0, 1, 0x01, 1)
		_write_cp_info("cp_prio_type", core_power.priority_type, 8, 1, 1, 0x01, 1)
	} else {
		/* get */
		_read_cp_info("cp_enable", core_power.enable, 16, 0, 0x01, 1)
		_read_cp_info("cp_prio_type", core_power.priority_type, 16, 1, 0x01, 1)
		core_power.supported = !!(punit_info->sst_header.cap_mask & BIT(0));
		if (copy_to_user(argp, &core_power, sizeof(core_power)))
			return -EFAULT;
	}

	return 0;
}

static long isst_if_clos_param(void __user *argp)
{
	struct tpmi_per_punit_info *punit_info;
	struct isst_clos_param clos_param;

	if (copy_from_user(&clos_param, argp, sizeof(clos_param)))
		return -EFAULT;

	punit_info = get_instance(clos_param.socket_id, clos_param.die_id);
	if (!punit_info)
		return -EINVAL;

	if (clos_param.get_set) {
		_write_cp_info("clos.min_freq", clos_param.min_freq_mhz, (24 + clos_param.clos * 8), 8, 8, 0xff, 100);
		_write_cp_info("clos.max_freq", clos_param.max_freq_mhz, (24 + clos_param.clos * 8), 16, 8, 0xff, 100);
		_write_cp_info("clos.prio", clos_param.prop_prio, (24 + clos_param.clos * 8), 4, 4, 0x0f, 1);
	} else {
		/* get */
		_read_cp_info("clos.min_freq", clos_param.min_freq_mhz, (24 + clos_param.clos * 8), 8, 0xff, 100)
		_read_cp_info("clos.max_freq", clos_param.max_freq_mhz, (24 + clos_param.clos * 8), 16, 0xff, 100)
		_read_cp_info("clos.prio", clos_param.prop_prio, (24 + clos_param.clos * 8), 4, 0x0f, 1)

		if (copy_to_user(argp, &clos_param, sizeof(clos_param)))
			return -EFAULT;
	}

	return 0;
}

static long isst_if_clos_assoc(void __user *argp)
{
	struct isst_if_clos_assoc_cmds assoc_cmds;
	unsigned char __user *ptr;
	long ret;
	int i;

	/* Each multi command has u16 command count as the first field */
	if (copy_from_user(&assoc_cmds, argp, sizeof(assoc_cmds)))
		return -EFAULT;

	if (!assoc_cmds.cmd_count)
		return -EINVAL;

	if (!assoc_cmds.punit_cpu_map)
		return -EINVAL;

	ptr = argp + offsetof(struct isst_if_clos_assoc_cmds, assoc_info);
	for (i = 0; i < assoc_cmds.cmd_count; ++i) {
		struct tpmi_per_punit_info *punit_info;
		struct isst_if_clos_assoc clos_assoc;
		int punit_id, punit_cpu_no, pkg_id;
		struct tpmi_sst_struct *sst_inst;
		int offset, shift;
		int cpu, clos;
		u64 val, mask;

		if (copy_from_user(&clos_assoc, ptr, sizeof(clos_assoc))) {
			ret = -EFAULT;
			break;
		}

		cpu = clos_assoc.logical_cpu;
		clos = clos_assoc.clos;

		punit_id = clos_assoc.die_id;
		pkg_id = clos_assoc.socket_id;

		sst_inst = sst_common.sst_inst[pkg_id];

		punit_info = &sst_inst->punit_info[punit_id];
		/* Start offset for clos assoc is 56 */
		offset = 56 + punit_cpu_no / 16;
		shift = punit_cpu_no % 16;
		shift *= 4;
		val = intel_tpmi_readq(punit_info->auxdev, punit_info->sst_base + punit_info->sst_header.cp_offset + offset);
		if (assoc_cmds.get_set) {
			mask = GENMASK_ULL((shift + 3), shift);
			val &= ~mask;
			val |= (clos << shift);
			intel_tpmi_writeq(punit_info->auxdev, val, punit_info->sst_base + punit_info->sst_header.cp_offset + offset);
		} else {
			val >>= shift;
			clos_assoc.clos = val & 0x0f;
			if (copy_to_user(ptr, &clos_assoc, sizeof(clos_assoc)))
				return -EFAULT;
		}

		ptr += sizeof(clos_assoc);
	}

	return 0;
}

#define _read_pp_info(name_str, name, offset, shift, mask, mult_factor)\
{\
	u64 val;\
	\
	val = intel_tpmi_readq(punit_info->auxdev, punit_info->sst_base + punit_info->sst_header.pp_offset + (offset));\
	val >>= shift;\
	val &= mask;\
	name = (val * mult_factor);\
	pr_debug("pp_info %s var:%s pp_offset:%x offset:%x shift:%x mask:%x mul_factor:%x res:0x%x\n",\
		__func__, name_str, punit_info->sst_header.pp_offset, offset, shift, mask, mult_factor, (u32)name);\
}

#define _write_pp_info(name_str, name, offset, shift, bits, mask, div_factor)\
{\
	u64 val, _mask;\
	\
	val = intel_tpmi_readq(punit_info->auxdev, punit_info->sst_base + punit_info->sst_header.pp_offset + (offset));\
	_mask = GENMASK((shift + bits - 1), shift);\
	val &= ~_mask;\
	val |= (name / div_factor) << shift;\
	intel_tpmi_writeq(punit_info->auxdev, val, punit_info->sst_base + punit_info->sst_header.pp_offset + (offset));\
	pr_debug("wr_pp_info %s var:%s wr:%x cp_offset:%x offset:%x shift:%x mask:%x div_factor:%x res:%llx\n",\
		__func__, name_str, name, punit_info->sst_header.pp_offset, offset, shift, mask, div_factor, val);\
}

#define _read_bf_level_info(name_str, name, level, offset, shift, mask, mult_factor)\
{\
	u64 val;\
	\
	val = intel_tpmi_readq(punit_info->auxdev, punit_info->sst_base + punit_info->perf_levels[level].mmio_offset +\
		(punit_info->feature_offsets.bf_offset * 8) + (offset));\
	val >>= shift;\
	val &= mask;\
	name = (val * mult_factor);\
	pr_debug("bf_info %s var:%s pp_level:%x level_offset:%x bf_offset:%x offset:%x shift:%d mask:%llx mul_factor:%x res:%x\n",\
                __func__, name_str, level, punit_info->perf_levels[level].mmio_offset, punit_info->feature_offsets.bf_offset * 8, offset, shift, (u64)mask, mult_factor, (u32)name);\
}

#define _read_tf_level_info(name_str, name, level, offset, shift, mask, mult_factor)\
{\
	u64 val;\
	\
	val = intel_tpmi_readq(punit_info->auxdev, punit_info->sst_base + punit_info->perf_levels[level].mmio_offset +\
		(punit_info->feature_offsets.tf_offset * 8) + (offset));\
	val >>= shift;\
	val &= mask;\
	name = (val * mult_factor);\
	pr_debug("tf_info %s var:%s pp_level:%x level_offset:%x tf_offset:%x offset:%x shift:%d mask:%llx mul_factor:%x res:%x\n",\
                __func__, name_str, level, punit_info->perf_levels[level].mmio_offset, punit_info->feature_offsets.tf_offset * 8, offset, shift, (u64)mask, mult_factor, (u32)name);\
}

static int isst_if_get_perf_level(void __user *argp)
{
	struct isst_perf_level_info perf_level;
	struct tpmi_per_punit_info *punit_info;

	if (copy_from_user(&perf_level, argp, sizeof(perf_level)))
		return -EFAULT;

	punit_info = get_instance(perf_level.socket_id, perf_level.die_id);
	if (!punit_info)
		return -EINVAL;

	perf_level.levels = punit_info->level_count;
	perf_level.level_mask = punit_info->pp_header.allowed_level_mask;
	perf_level.feature_rev = punit_info->pp_header.feature_rev;
	_read_pp_info("current_level", perf_level.current_level, 32, 0, 0x03, 1)
	_read_pp_info("locked", perf_level.locked, 32, 3, 0x01, 1)
	_read_pp_info("feature_state", perf_level.feature_state, 32, 8, 0xff, 1)
	perf_level.enabled = !!(punit_info->sst_header.cap_mask & BIT(1));

	_read_bf_level_info("bf_support", perf_level.sst_bf_support, 0 , 0, 12, 0x1, 1);
	_read_tf_level_info("tf_support", perf_level.sst_tf_support, 0 , 0, 12, 0x1, 1);

	if (copy_to_user(argp, &perf_level, sizeof(perf_level)))
		return -EFAULT;

	return 0;
}

static int isst_if_set_perf_level(void __user *argp)
{
	struct isst_perf_level_control perf_level;
	struct tpmi_per_punit_info *punit_info;

	if (copy_from_user(&perf_level, argp, sizeof(perf_level)))
		return -EFAULT;

	punit_info = get_instance(perf_level.socket_id, perf_level.die_id);
	if (!punit_info)
		return -EINVAL;

	_write_pp_info("perf_level", perf_level.level, 24, 0, 3, 0x07, 1)

	return 0;
}

static int isst_if_set_perf_feature(void __user *argp)
{
	struct isst_perf_feature_control perf_feature;
	struct tpmi_per_punit_info *punit_info;

	if (copy_from_user(&perf_feature, argp, sizeof(perf_feature)))
		return -EFAULT;

	punit_info = get_instance(perf_feature.socket_id, perf_feature.die_id);
	if (!punit_info)
		return -EINVAL;

	_write_pp_info("perf_feature", perf_feature.feature, 24, 8, 8, 0xff, 1)

	return 0;
}

#define _read_pp_level_info(name_str, name, level, offset, shift, mask, mult_factor)\
{\
	u64 val;\
	\
	val = intel_tpmi_readq(punit_info->auxdev, punit_info->sst_base + punit_info->perf_levels[level].mmio_offset +\
		(punit_info->feature_offsets.pp_offset * 8) + (offset));\
	val >>= shift;\
	val &= mask;\
	name = (val * mult_factor);\
	pr_debug("pp_level_info %s var:%s pp_level:%x level_offset:%x offset:%x shift:%x mask:%llx mul_factor:%x res:%x\n",\
                __func__, name_str, level, punit_info->perf_levels[level].mmio_offset, offset, shift, (u64)mask, mult_factor, (u32)name);\
}

static int isst_if_get_perf_level_info(void __user *argp)
{
	struct isst_perf_level_data_info perf_level;
	struct tpmi_per_punit_info *punit_info;

	if (copy_from_user(&perf_level, argp, sizeof(perf_level)))
		return -EFAULT;

	punit_info = get_instance(perf_level.socket_id, perf_level.die_id);
	if (!punit_info)
		return -EINVAL;

	if (perf_level.level >= punit_info->level_count)
		return -EINVAL;

	_read_pp_level_info("tdp_ratio", perf_level.tdp_ratio, perf_level.level, 0, 0, 0xff, 1)
	_read_pp_level_info("base_freq_mhz", perf_level.base_freq_mhz, perf_level.level, 0, 0, 0xff, 100)
	_read_pp_level_info("base_freq_avx2_mhz", perf_level.base_freq_avx2_mhz, perf_level.level, 0, 8, 0xff, 100)
	_read_pp_level_info("base_freq_avx512_mhz", perf_level.base_freq_avx512_mhz, perf_level.level, 0, 16, 0xff, 100)
	_read_pp_level_info("base_freq_amx_mhz", perf_level.base_freq_amx_mhz, perf_level.level, 0, 24, 0xff, 100)

	_read_pp_level_info("thermal_design_power_w", perf_level.thermal_design_power_w, perf_level.level, 8, 32, 0x7fff, 1)
	perf_level.thermal_design_power_w /= 8;
	_read_pp_level_info("tjunction_max_c", perf_level.tjunction_max_c, perf_level.level, 8, 47, 0xff, 1)
	_read_pp_level_info("max_memory_freq_mhz", perf_level.max_memory_freq_mhz, perf_level.level, 8, 55, 0x1f, 100)
	_read_pp_level_info("cooling_type", perf_level.cooling_type, perf_level.level, 8, 60, 0x07,1)

	_read_pp_level_info("cdyn0_bucket0_freq_mhz", perf_level.cdyn0_bucket0_freq_mhz, perf_level.level, 32, 0, 0xff, 100);
	_read_pp_level_info("cdyn0_bucket1_freq_mhz", perf_level.cdyn0_bucket1_freq_mhz, perf_level.level, 32, 8, 0xff, 100);
	_read_pp_level_info("cdyn0_bucket2_freq_mhz", perf_level.cdyn0_bucket2_freq_mhz, perf_level.level, 32, 16, 0xff, 100);
	_read_pp_level_info("cdyn0_bucket3_freq_mhz", perf_level.cdyn0_bucket3_freq_mhz, perf_level.level, 32, 24, 0xff, 100);
	_read_pp_level_info("cdyn0_bucket4_freq_mhz", perf_level.cdyn0_bucket4_freq_mhz, perf_level.level, 32, 32, 0xff, 100);
	_read_pp_level_info("cdyn0_bucket5_freq_mhz", perf_level.cdyn0_bucket5_freq_mhz, perf_level.level, 32, 40, 0xff, 100);
	_read_pp_level_info("cdyn0_bucket6_freq_mhz", perf_level.cdyn0_bucket6_freq_mhz, perf_level.level, 32, 48, 0xff, 100);
	_read_pp_level_info("cdyn0_bucket7_freq_mhz", perf_level.cdyn0_bucket7_freq_mhz, perf_level.level, 32, 56, 0xff, 100);

	_read_pp_level_info("cdyn1_bucket0_freq_mhz", perf_level.cdyn1_bucket0_freq_mhz, perf_level.level, 40, 0, 0xff, 100);
	_read_pp_level_info("cdyn1_bucket1_freq_mhz", perf_level.cdyn1_bucket1_freq_mhz, perf_level.level, 40, 8, 0xff, 100);
	_read_pp_level_info("cdyn1_bucket2_freq_mhz", perf_level.cdyn1_bucket2_freq_mhz, perf_level.level, 40, 16, 0xff, 100);
	_read_pp_level_info("cdyn1_bucket3_freq_mhz", perf_level.cdyn1_bucket3_freq_mhz, perf_level.level, 40, 24, 0xff, 100);
	_read_pp_level_info("cdyn1_bucket4_freq_mhz", perf_level.cdyn1_bucket4_freq_mhz, perf_level.level, 40, 32, 0xff, 100);
	_read_pp_level_info("cdyn1_bucket5_freq_mhz", perf_level.cdyn1_bucket5_freq_mhz, perf_level.level, 40, 40, 0xff, 100);
	_read_pp_level_info("cdyn1_bucket6_freq_mhz", perf_level.cdyn1_bucket6_freq_mhz, perf_level.level, 40, 48, 0xff, 100);
	_read_pp_level_info("cdyn1_bucket7_freq_mhz", perf_level.cdyn1_bucket7_freq_mhz, perf_level.level, 40, 56, 0xff, 100);

	_read_pp_level_info("cdyn2_bucket0_freq_mhz", perf_level.cdyn2_bucket0_freq_mhz, perf_level.level, 48, 0, 0xff, 100);
	_read_pp_level_info("cdyn2_bucket1_freq_mhz", perf_level.cdyn2_bucket1_freq_mhz, perf_level.level, 48, 8, 0xff, 100);
	_read_pp_level_info("cdyn2_bucket2_freq_mhz", perf_level.cdyn2_bucket2_freq_mhz, perf_level.level, 48, 16, 0xff, 100);
	_read_pp_level_info("cdyn2_bucket3_freq_mhz", perf_level.cdyn2_bucket3_freq_mhz, perf_level.level, 48, 24, 0xff, 100);
	_read_pp_level_info("cdyn2_bucket4_freq_mhz", perf_level.cdyn2_bucket4_freq_mhz, perf_level.level, 48, 32, 0xff, 100);
	_read_pp_level_info("cdyn2_bucket5_freq_mhz", perf_level.cdyn2_bucket5_freq_mhz, perf_level.level, 48, 40, 0xff, 100);
	_read_pp_level_info("cdyn2_bucket6_freq_mhz", perf_level.cdyn2_bucket6_freq_mhz, perf_level.level, 48, 48, 0xff, 100);
	_read_pp_level_info("cdyn2_bucket7_freq_mhz", perf_level.cdyn2_bucket7_freq_mhz, perf_level.level, 48, 56, 0xff, 100);

	_read_pp_level_info("cdyn3_bucket0_freq_mhz", perf_level.cdyn3_bucket0_freq_mhz, perf_level.level, 56, 0, 0xff, 100);
	_read_pp_level_info("cdyn3_bucket1_freq_mhz", perf_level.cdyn3_bucket1_freq_mhz, perf_level.level, 56, 8, 0xff, 100);
	_read_pp_level_info("cdyn3_bucket2_freq_mhz", perf_level.cdyn3_bucket2_freq_mhz, perf_level.level, 56, 16, 0xff, 100);
	_read_pp_level_info("cdyn3_bucket3_freq_mhz", perf_level.cdyn3_bucket3_freq_mhz, perf_level.level, 56, 24, 0xff, 100);
	_read_pp_level_info("cdyn3_bucket4_freq_mhz", perf_level.cdyn3_bucket4_freq_mhz, perf_level.level, 56, 32, 0xff, 100);
	_read_pp_level_info("cdyn3_bucket5_freq_mhz", perf_level.cdyn3_bucket5_freq_mhz, perf_level.level, 56, 40, 0xff, 100);
	_read_pp_level_info("cdyn3_bucket6_freq_mhz", perf_level.cdyn3_bucket6_freq_mhz, perf_level.level, 56, 48, 0xff, 100);
	_read_pp_level_info("cdyn3_bucket7_freq_mhz", perf_level.cdyn3_bucket7_freq_mhz, perf_level.level, 56, 56, 0xff, 100);

	_read_pp_level_info("cdyn4_bucket0_freq_mhz", perf_level.cdyn4_bucket0_freq_mhz, perf_level.level, 64, 0, 0xff, 100);
	_read_pp_level_info("cdyn4_bucket1_freq_mhz", perf_level.cdyn4_bucket1_freq_mhz, perf_level.level, 64, 8, 0xff, 100);
	_read_pp_level_info("cdyn4_bucket2_freq_mhz", perf_level.cdyn4_bucket2_freq_mhz, perf_level.level, 64, 16, 0xff, 100);
	_read_pp_level_info("cdyn4_bucket3_freq_mhz", perf_level.cdyn4_bucket3_freq_mhz, perf_level.level, 64, 24, 0xff, 100);
	_read_pp_level_info("cdyn4_bucket4_freq_mhz", perf_level.cdyn4_bucket4_freq_mhz, perf_level.level, 64, 32, 0xff, 100);
	_read_pp_level_info("cdyn4_bucket5_freq_mhz", perf_level.cdyn4_bucket5_freq_mhz, perf_level.level, 64, 40, 0xff, 100);
	_read_pp_level_info("cdyn4_bucket6_freq_mhz", perf_level.cdyn4_bucket6_freq_mhz, perf_level.level, 64, 48, 0xff, 100);
	_read_pp_level_info("cdyn4_bucket7_freq_mhz", perf_level.cdyn4_bucket7_freq_mhz, perf_level.level, 64, 56, 0xff, 100);

	_read_pp_level_info("cdyn5_bucket0_freq_mhz", perf_level.cdyn5_bucket0_freq_mhz, perf_level.level, 72, 0, 0xff, 100);
	_read_pp_level_info("cdyn5_bucket1_freq_mhz", perf_level.cdyn5_bucket1_freq_mhz, perf_level.level, 72, 8, 0xff, 100);
	_read_pp_level_info("cdyn5_bucket2_freq_mhz", perf_level.cdyn5_bucket2_freq_mhz, perf_level.level, 72, 16, 0xff, 100);
	_read_pp_level_info("cdyn5_bucket3_freq_mhz", perf_level.cdyn5_bucket3_freq_mhz, perf_level.level, 72, 24, 0xff, 100);
	_read_pp_level_info("cdyn5_bucket4_freq_mhz", perf_level.cdyn5_bucket4_freq_mhz, perf_level.level, 72, 32, 0xff, 100);
	_read_pp_level_info("cdyn5_bucket5_freq_mhz", perf_level.cdyn5_bucket5_freq_mhz, perf_level.level, 72, 40, 0xff, 100);
	_read_pp_level_info("cdyn5_bucket6_freq_mhz", perf_level.cdyn5_bucket6_freq_mhz, perf_level.level, 72, 48, 0xff, 100);
	_read_pp_level_info("cdyn5_bucket7_freq_mhz", perf_level.cdyn5_bucket7_freq_mhz, perf_level.level, 72, 56, 0xff, 100);

	_read_pp_level_info("bucket0_core_count", perf_level.bucket0_core_count, perf_level.level, 80, 0, 0xff, 1)
	_read_pp_level_info("bucket1_core_count", perf_level.bucket1_core_count, perf_level.level, 80, 8, 0xff, 1)
	_read_pp_level_info("bucket2_core_count", perf_level.bucket2_core_count, perf_level.level, 80, 16, 0xff, 1)
	_read_pp_level_info("bucket3_core_count", perf_level.bucket3_core_count, perf_level.level, 80, 24, 0xff, 1)
	_read_pp_level_info("bucket4_core_count", perf_level.bucket4_core_count, perf_level.level, 80, 32, 0xff, 1)
	_read_pp_level_info("bucket5_core_count", perf_level.bucket5_core_count, perf_level.level, 80, 40, 0xff, 1)
	_read_pp_level_info("bucket6_core_count", perf_level.bucket6_core_count, perf_level.level, 80, 48, 0xff, 1)
	_read_pp_level_info("bucket7_core_count", perf_level.bucket7_core_count, perf_level.level, 80, 56, 0xff, 1)

	_read_pp_level_info("p0_core_ratio", perf_level.p0_core_ratio, perf_level.level, 88, 0, 0xff, 1)
	_read_pp_level_info("p1_core_ratio", perf_level.p1_core_ratio, perf_level.level, 88, 8, 0xff, 1)
	_read_pp_level_info("pn_core_ratio", perf_level.pn_core_ratio, perf_level.level, 88, 16, 0xff, 1)
	_read_pp_level_info("pm_core_ratio", perf_level.pm_core_ratio, perf_level.level, 88, 24, 0xff, 1)
	_read_pp_level_info("p0_fabric_ratio", perf_level.p0_fabric_ratio, perf_level.level, 88, 32, 0xff, 1)
	_read_pp_level_info("p1_fabric_ratio", perf_level.p1_fabric_ratio, perf_level.level, 88, 40, 0xff, 1)
	_read_pp_level_info("pn_fabric_ratio", perf_level.pn_fabric_ratio, perf_level.level, 88, 48, 0xff, 1)
	_read_pp_level_info("pm_fabric_ratio", perf_level.pm_fabric_ratio, perf_level.level, 88, 56, 0xff, 1)

	if (copy_to_user(argp, &perf_level, sizeof(perf_level)))
		return -EFAULT;

	return 0;
}

static int isst_if_get_perf_level_mask(void __user *argp)
{
	static struct isst_perf_level_cpu_mask cpumask;
	struct tpmi_per_punit_info *punit_info;
	u32 count;
	u64 mask;

	if (copy_from_user(&cpumask, argp, sizeof(cpumask)))
		return -EFAULT;

	punit_info = get_instance(cpumask.socket_id, cpumask.die_id);
	if (!punit_info)
		return -EINVAL;

	_read_pp_level_info("count", count, cpumask.level, 8, 8, 0xff, 1)
	_read_pp_level_info("mask", mask, cpumask.level, 16, 0, 0xffffffffffffffff, 1)

	cpumask.punit_cpu_map = 1;
	cpumask.mask = mask;

	if (copy_to_user(argp, &cpumask, sizeof(cpumask)))
		return -EFAULT;

	return 0;
}

static int isst_if_get_base_freq_info(void __user *argp)
{
	static struct isst_base_freq_info base_freq;
	struct tpmi_per_punit_info *punit_info;

	if (copy_from_user(&base_freq, argp, sizeof(base_freq)))
		return -EFAULT;

	punit_info = get_instance(base_freq.socket_id, base_freq.die_id);
	if (!punit_info)
		return -EINVAL;

	_read_bf_level_info("p1_high", base_freq.high_base_freq_mhz, base_freq.level, 0, 13, 0xff, 100)
	_read_bf_level_info("p1_low", base_freq.low_base_freq_mhz, base_freq.level, 0, 21, 0xff, 100)
	_read_bf_level_info("BF-TJ", base_freq.tjunction_max_c, base_freq.level, 0, 35, 0xff, 1)
	_read_bf_level_info("BF-tdp", base_freq.thermal_design_power_w, base_freq.level, 0, 43, 0x7fff, 1)
	base_freq.thermal_design_power_w /= 8;

	if (copy_to_user(argp, &base_freq, sizeof(base_freq)))
		return -EFAULT;

	return 0;
}

static int isst_if_get_base_freq_mask(void __user *argp)
{
	static struct isst_perf_level_cpu_mask cpumask;
	struct tpmi_per_punit_info *punit_info;
	u64 mask;

	if (copy_from_user(&cpumask, argp, sizeof(cpumask)))
		return -EFAULT;

	punit_info = get_instance(cpumask.socket_id, cpumask.die_id);
	if (!punit_info)
		return -EINVAL;

	_read_bf_level_info("BF-cpumask", mask, cpumask.level, 8, 0, 0xffffffffffffffff, 1)

	cpumask.punit_cpu_map = 1;
	cpumask.mask = mask;

	if (copy_to_user(argp, &cpumask, sizeof(cpumask)))
		return -EFAULT;

	return 0;
}

static int isst_if_get_tpmi_instance_count(void __user *argp)
{
	struct isst_tpmi_instance_count tpmi_inst;
	struct tpmi_sst_struct *sst_inst;
	int i;

	if (copy_from_user(&tpmi_inst, argp, sizeof(tpmi_inst)))
		return -EFAULT;

	if (tpmi_inst.socket_id >= SST_MAX_INSTANCES)
		return -EINVAL;

	tpmi_inst.count = sst_common.sst_inst[tpmi_inst.socket_id]->number_of_punits;

	sst_inst = sst_common.sst_inst[tpmi_inst.socket_id];
	tpmi_inst.valid_mask = 0;
	for (i = 0; i < sst_inst->number_of_punits; ++i) {
		struct tpmi_per_punit_info *punit_info;

		punit_info = &sst_inst->punit_info[i];
		if (punit_info->sst_base)
			tpmi_inst.valid_mask |= BIT(i);
	}
	if (copy_to_user(argp, &tpmi_inst, sizeof(tpmi_inst)))
		return -EFAULT;

	return 0;
}

static int isst_if_get_turbo_freq_info(void __user *argp)
{
	static struct isst_turbo_freq_info turbo_freq;
	struct tpmi_per_punit_info *punit_info;

	if (copy_from_user(&turbo_freq, argp, sizeof(turbo_freq)))
		return -EFAULT;

	punit_info = get_instance(turbo_freq.socket_id, turbo_freq.die_id);
	if (!punit_info)
		return -EINVAL;

	_read_tf_level_info("lp_clip0", turbo_freq.lp_clip_0_mhz, turbo_freq.level, 0, 16, 0xff, 100)
	_read_tf_level_info("lp_clip1", turbo_freq.lp_clip_1_mhz, turbo_freq.level, 0, 24, 0xff, 100)
	_read_tf_level_info("lp_clip2", turbo_freq.lp_clip_2_mhz, turbo_freq.level, 0, 32, 0xff, 100)
	_read_tf_level_info("lp_clip3", turbo_freq.lp_clip_3_mhz, turbo_freq.level, 0, 40, 0xff, 100)

	_read_tf_level_info("bucket_0_core_count", turbo_freq.bucket_0_core_count, turbo_freq.level, 8, 0, 0xff, 1)
	_read_tf_level_info("bucket_1_core_count", turbo_freq.bucket_1_core_count, turbo_freq.level, 8, 8, 0xff, 1)
	_read_tf_level_info("bucket_2_core_count", turbo_freq.bucket_2_core_count, turbo_freq.level, 8, 16, 0xff, 1)
	_read_tf_level_info("bucket_3_core_count", turbo_freq.bucket_3_core_count, turbo_freq.level, 8, 24, 0xff, 1)
	_read_tf_level_info("bucket_4_core_count", turbo_freq.bucket_4_core_count, turbo_freq.level, 8, 32, 0xff, 1)
	_read_tf_level_info("bucket_5_core_count", turbo_freq.bucket_5_core_count, turbo_freq.level, 8, 40, 0xff, 1)
	_read_tf_level_info("bucket_6_core_count", turbo_freq.bucket_6_core_count, turbo_freq.level, 8, 48, 0xff, 1)
	_read_tf_level_info("bucket_7_core_count", turbo_freq.bucket_7_core_count, turbo_freq.level, 8, 56, 0xff, 1)

	_read_tf_level_info("bucket_0,cydn_level_0_trl", turbo_freq.bucket_0_cydn_level_0_trl, turbo_freq.level, 16, 0, 0xff, 100)
	_read_tf_level_info("bucket_1,cydn_level_0_trl", turbo_freq.bucket_1_cydn_level_0_trl, turbo_freq.level, 16, 8, 0xff, 100)
	_read_tf_level_info("bucket_2,cydn_level_0_trl", turbo_freq.bucket_2_cydn_level_0_trl, turbo_freq.level, 16, 16, 0xff, 100)
	_read_tf_level_info("bucket_3,cydn_level_0_trl", turbo_freq.bucket_3_cydn_level_0_trl, turbo_freq.level, 16, 24, 0xff, 100)
	_read_tf_level_info("bucket_4,cydn_level_0_trl", turbo_freq.bucket_4_cydn_level_0_trl, turbo_freq.level, 16, 32, 0xff, 100)
	_read_tf_level_info("bucket_5,cydn_level_0_trl", turbo_freq.bucket_5_cydn_level_0_trl, turbo_freq.level, 16, 40, 0xff, 100)
	_read_tf_level_info("bucket_6,cydn_level_0_trl", turbo_freq.bucket_6_cydn_level_0_trl, turbo_freq.level, 16, 48, 0xff, 100)
	_read_tf_level_info("bucket_7,cydn_level_0_trl", turbo_freq.bucket_7_cydn_level_0_trl, turbo_freq.level, 16, 56, 0xff, 100)

	_read_tf_level_info("bucket_0,cydn_level_1_trl", turbo_freq.bucket_0_cydn_level_1_trl, turbo_freq.level, 24, 0, 0xff, 100)
	_read_tf_level_info("bucket_1,cydn_level_1_trl", turbo_freq.bucket_1_cydn_level_1_trl, turbo_freq.level, 24, 8, 0xff, 100)
	_read_tf_level_info("bucket_2,cydn_level_1_trl", turbo_freq.bucket_2_cydn_level_1_trl, turbo_freq.level, 24, 16, 0xff, 100)
	_read_tf_level_info("bucket_3,cydn_level_1_trl", turbo_freq.bucket_3_cydn_level_1_trl, turbo_freq.level, 24, 24, 0xff, 100)
	_read_tf_level_info("bucket_4,cydn_level_1_trl", turbo_freq.bucket_4_cydn_level_1_trl, turbo_freq.level, 24, 32, 0xff, 100)
	_read_tf_level_info("bucket_5,cydn_level_1_trl", turbo_freq.bucket_5_cydn_level_1_trl, turbo_freq.level, 24, 40, 0xff, 100)
	_read_tf_level_info("bucket_6,cydn_level_1_trl", turbo_freq.bucket_6_cydn_level_1_trl, turbo_freq.level, 24, 48, 0xff, 100)
	_read_tf_level_info("bucket_7,cydn_level_1_trl", turbo_freq.bucket_7_cydn_level_1_trl, turbo_freq.level, 24, 56, 0xff, 100)

	_read_tf_level_info("bucket_0,cydn_level_2_trl", turbo_freq.bucket_0_cydn_level_2_trl, turbo_freq.level, 32, 0, 0xff, 100)
	_read_tf_level_info("bucket_1,cydn_level_2_trl", turbo_freq.bucket_1_cydn_level_2_trl, turbo_freq.level, 32, 8, 0xff, 100)
	_read_tf_level_info("bucket_2,cydn_level_2_trl", turbo_freq.bucket_2_cydn_level_2_trl, turbo_freq.level, 32, 16, 0xff, 100)
	_read_tf_level_info("bucket_3,cydn_level_2_trl", turbo_freq.bucket_3_cydn_level_2_trl, turbo_freq.level, 32, 24, 0xff, 100)
	_read_tf_level_info("bucket_4,cydn_level_2_trl", turbo_freq.bucket_4_cydn_level_2_trl, turbo_freq.level, 32, 32, 0xff, 100)
	_read_tf_level_info("bucket_5,cydn_level_2_trl", turbo_freq.bucket_5_cydn_level_2_trl, turbo_freq.level, 32, 40, 0xff, 100)
	_read_tf_level_info("bucket_6,cydn_level_2_trl", turbo_freq.bucket_6_cydn_level_2_trl, turbo_freq.level, 32, 48, 0xff, 100)
	_read_tf_level_info("bucket_7,cydn_level_2_trl", turbo_freq.bucket_7_cydn_level_2_trl, turbo_freq.level, 32, 56, 0xff, 100)

	_read_tf_level_info("bucket_0,cydn_level_3_trl", turbo_freq.bucket_0_cydn_level_3_trl, turbo_freq.level, 40, 0, 0xff, 100)
	_read_tf_level_info("bucket_1,cydn_level_3_trl", turbo_freq.bucket_1_cydn_level_3_trl, turbo_freq.level, 40, 8, 0xff, 100)
	_read_tf_level_info("bucket_2,cydn_level_3_trl", turbo_freq.bucket_2_cydn_level_3_trl, turbo_freq.level, 40, 16, 0xff, 100)
	_read_tf_level_info("bucket_3,cydn_level_3_trl", turbo_freq.bucket_3_cydn_level_3_trl, turbo_freq.level, 40, 24, 0xff, 100)
	_read_tf_level_info("bucket_4,cydn_level_3_trl", turbo_freq.bucket_4_cydn_level_3_trl, turbo_freq.level, 40, 32, 0xff, 100)
	_read_tf_level_info("bucket_5,cydn_level_3_trl", turbo_freq.bucket_5_cydn_level_3_trl, turbo_freq.level, 40, 40, 0xff, 100)
	_read_tf_level_info("bucket_6,cydn_level_3_trl", turbo_freq.bucket_6_cydn_level_3_trl, turbo_freq.level, 40, 48, 0xff, 100)
	_read_tf_level_info("bucket_7,cydn_level_3_trl", turbo_freq.bucket_7_cydn_level_3_trl, turbo_freq.level, 40, 56, 0xff, 100)

	_read_tf_level_info("bucket_0,cydn_level_4_trl", turbo_freq.bucket_0_cydn_level_4_trl, turbo_freq.level, 48, 0, 0xff, 100)
	_read_tf_level_info("bucket_1,cydn_level_4_trl", turbo_freq.bucket_1_cydn_level_4_trl, turbo_freq.level, 48, 8, 0xff, 100)
	_read_tf_level_info("bucket_2,cydn_level_4_trl", turbo_freq.bucket_2_cydn_level_4_trl, turbo_freq.level, 48, 16, 0xff, 100)
	_read_tf_level_info("bucket_3,cydn_level_4_trl", turbo_freq.bucket_3_cydn_level_4_trl, turbo_freq.level, 48, 24, 0xff, 100)
	_read_tf_level_info("bucket_4,cydn_level_4_trl", turbo_freq.bucket_4_cydn_level_4_trl, turbo_freq.level, 48, 32, 0xff, 100)
	_read_tf_level_info("bucket_5,cydn_level_4_trl", turbo_freq.bucket_5_cydn_level_4_trl, turbo_freq.level, 48, 40, 0xff, 100)
	_read_tf_level_info("bucket_6,cydn_level_4_trl", turbo_freq.bucket_6_cydn_level_4_trl, turbo_freq.level, 48, 48, 0xff, 100)
	_read_tf_level_info("bucket_7,cydn_level_4_trl", turbo_freq.bucket_7_cydn_level_4_trl, turbo_freq.level, 48, 56, 0xff, 100)

	_read_tf_level_info("bucket_0,cydn_level_5_trl", turbo_freq.bucket_0_cydn_level_5_trl, turbo_freq.level, 56, 0, 0xff, 100)
	_read_tf_level_info("bucket_1,cydn_level_5_trl", turbo_freq.bucket_1_cydn_level_5_trl, turbo_freq.level, 56, 8, 0xff, 100)
	_read_tf_level_info("bucket_2,cydn_level_5_trl", turbo_freq.bucket_2_cydn_level_5_trl, turbo_freq.level, 56, 16, 0xff, 100)
	_read_tf_level_info("bucket_3,cydn_level_5_trl", turbo_freq.bucket_3_cydn_level_5_trl, turbo_freq.level, 56, 24, 0xff, 100)
	_read_tf_level_info("bucket_4,cydn_level_5_trl", turbo_freq.bucket_4_cydn_level_5_trl, turbo_freq.level, 56, 32, 0xff, 100)
	_read_tf_level_info("bucket_5,cydn_level_5_trl", turbo_freq.bucket_5_cydn_level_5_trl, turbo_freq.level, 56, 40, 0xff, 100)
	_read_tf_level_info("bucket_6,cydn_level_5_trl", turbo_freq.bucket_6_cydn_level_5_trl, turbo_freq.level, 56, 48, 0xff, 100)
	_read_tf_level_info("bucket_7,cydn_level_5_trl", turbo_freq.bucket_7_cydn_level_5_trl, turbo_freq.level, 56, 56, 0xff, 100)

	if (copy_to_user(argp, &turbo_freq, sizeof(turbo_freq)))
		return -EFAULT;

	return 0;
}

static int sst_add_perf_profiles(struct auxiliary_device *auxdev, struct tpmi_per_punit_info *punit_info, int levels)
{
	int i, mask, perf_index;
	u64 perf_level_offsets;

	punit_info->perf_levels = devm_kcalloc(&auxdev->dev, levels, sizeof(struct perf_level), GFP_KERNEL);
	if (!punit_info->perf_levels)
		return 0;

	punit_info->ratio_unit = punit_info->pp_header.ratio_unit;
	punit_info->avx_levels = punit_info->pp_header.num_avx_levels;
	punit_info->pp_block_size = punit_info->pp_header.block_size;

	/* Read PP Offset 0: Get feature offset with PP level */
	*((u64 *)&punit_info->feature_offsets) = readq(punit_info->sst_base + punit_info->sst_header.pp_offset + 8);
	dev_dbg(&auxdev->dev, "perf-level pp_offset:%x bf_offset:%x tf_offset:%x\n", punit_info->feature_offsets.pp_offset,
		punit_info->feature_offsets.bf_offset, punit_info->feature_offsets.tf_offset);

	perf_level_offsets = readq(punit_info->sst_base + punit_info->sst_header.pp_offset + 16);
	dev_dbg(&auxdev->dev, "perf-level-offsets :%llx\n", perf_level_offsets);

	mask = 0x01;
	perf_index = 0;

	for (i = 0; i < levels; ++i) {
		u64 offset;

		offset = perf_level_offsets & (0xff << (i * 8));
		offset >>= (i * 8);
		offset &= 0xff;
		offset *= 8;
		punit_info->perf_levels[perf_index].mmio_offset = punit_info->sst_header.pp_offset + offset;
		mask <<= 1;
		dev_dbg(&auxdev->dev, "perf-level:%x offset:%llx\n", i, offset);
	}

	return 0;
}

static int sst_main(struct auxiliary_device *auxdev, struct tpmi_per_punit_info *punit_info)
{
	int i, mask, levels;

	*((u64 *)&punit_info->sst_header) = readq(punit_info->sst_base);
	punit_info->sst_header.cp_offset *= 8;
	punit_info->sst_header.pp_offset *= 8;
	dev_dbg(&auxdev->dev, "SST header: interface_ver:0x%x cap_mask:0x%x cp_off:0x%x pp_off:0x%x\n",
		punit_info->sst_header.interface_version,
		punit_info->sst_header.cap_mask,
		punit_info->sst_header.cp_offset,
		punit_info->sst_header.pp_offset);

	if (punit_info->sst_header.interface_version != ISST_HEADER_VERSION) {
		dev_err(&auxdev->dev, "SST: Unsupported version:%x\n", punit_info->sst_header.interface_version);
		return -ENODEV;
	}

	/* Read SST CP Header */
	*((u64 *)&punit_info->cp_header) = readq(punit_info->sst_base + punit_info->sst_header.cp_offset);
	dev_dbg(&auxdev->dev, "CP header: feature_id:0x%x rev:0x%x ratio_unit:0x%x\n",
		punit_info->cp_header.feature_id,
		punit_info->cp_header.feature_rev,
		punit_info->cp_header.ratio_unit
		);

	/* Read PP header */
	*((u64 *)&punit_info->pp_header) = readq(punit_info->sst_base + punit_info->sst_header.pp_offset);
	dev_dbg(&auxdev->dev, "PP header: feature_id:0x%x rev:0x%x level_en_mask:0x%x allowed_lev_mask:0x%x avx_levels:0x%x ratio_unit:0x%x block_size:0x%x\n",
		punit_info->pp_header.feature_id,
		punit_info->pp_header.feature_rev,
		punit_info->pp_header.level_en_mask,
		punit_info->pp_header.allowed_level_mask,
		punit_info->pp_header.num_avx_levels,
		punit_info->pp_header.ratio_unit,
		punit_info->pp_header.block_size
		);

	/* Force allowed mask level 0*/
	punit_info->pp_header.allowed_level_mask |= 0x01;

	mask = 0x01;
	levels = 0;
	for (i = 0; i < 8; ++i) {
		if (punit_info->pp_header.allowed_level_mask & mask)
			++levels;
		mask <<= 1;
	}
	punit_info->level_count = levels;
	dev_dbg(&auxdev->dev, "Number of perf levels %x\n", levels);
	sst_add_perf_profiles(auxdev, punit_info, levels);

	return 0;
}

static int isst_if_get_platform_info(void __user *argp)
{
	struct isst_if_platform_info info;

	info.api_version = TPMI_ISST_IF_API_VERSION,
	info.driver_version = TPMI_ISST_IF_DRIVER_VERSION,
	info.max_cmds_per_ioctl = TPMI_ISST_IF_CMD_LIMIT,
	info.mbox_supported = 0;
	info.mmio_supported = 0;

	if (copy_to_user(argp, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static long isst_if_def_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	long ret = -ENOTTY;

	mutex_lock(&iss_tpmi_dev_lock);
	switch (cmd) {
	case ISST_IF_GET_PLATFORM_INFO:
		ret = isst_if_get_platform_info(argp);
		break;
	case ISST_IF_CORE_POWER_STATE:
		ret = isst_if_core_power_state(argp);
		break;
	case ISST_IF_CLOS_PARAM:
		ret = isst_if_clos_param(argp);
		break;
	case ISST_IF_CLOS_ASSOC:
		ret = isst_if_clos_assoc(argp);
		break;
	case ISST_IF_PERF_LEVELS:
		ret = isst_if_get_perf_level(argp);
		break;
	case ISST_IF_PERF_SET_LEVEL:
		ret = isst_if_set_perf_level(argp);
		break;
	case ISST_IF_PERF_SET_FEATURE:
		ret = isst_if_set_perf_feature(argp);
		break;
	case ISST_IF_GET_PERF_LEVEL_INFO:
		ret = isst_if_get_perf_level_info(argp);
		break;
	case ISST_IF_GET_PERF_LEVEL_CPU_MASK:
		ret = isst_if_get_perf_level_mask(argp);
		break;
	case ISST_IF_GET_BASE_FREQ_INFO:
		ret = isst_if_get_base_freq_info(argp);
		break;
	case ISST_IF_GET_BASE_FREQ_CPU_MASK:
		ret = isst_if_get_base_freq_mask(argp);
		break;
	case ISST_IF_GET_TURBO_FREQ_INFO:
		ret = isst_if_get_turbo_freq_info(argp);
		break;
	case ISST_IF_COUNT_TPMI_INSTANCES:
		ret = isst_if_get_tpmi_instance_count(argp);
		break;
	default:
		break;
	}
	mutex_unlock(&iss_tpmi_dev_lock);

	return ret;
}

int tpmi_sst_dev_add(struct auxiliary_device *auxdev)
{
	struct intel_tpmi_plat_info *plat_info;
	struct tpmi_sst_struct *tpmi_sst;
	int num_resources;
	int i, ret, pkg = 0, inst = 0;

	plat_info = tpmi_get_platform_data(auxdev);
	if (!plat_info) {
		dev_info(&auxdev->dev, "No platform info\n");
		return -EINVAL;
	}

	pkg = plat_info->package_id;
	if (pkg >= SST_MAX_INSTANCES) {
		dev_info(&auxdev->dev, "Invalid packae id :%x\n", pkg);
		return -EINVAL;
	}

	if (sst_common.sst_inst[pkg])
		return -EEXIST;

	num_resources = tpmi_get_resource_count(auxdev);
	dev_dbg(&auxdev->dev, "Number of resources:%x \n", num_resources);

	if (!num_resources)
		return -EINVAL;

	tpmi_sst = devm_kzalloc(&auxdev->dev, sizeof(*tpmi_sst), GFP_KERNEL);
	if (!tpmi_sst)
		return -ENOMEM;

	tpmi_sst->punit_info = devm_kcalloc(&auxdev->dev, num_resources,
					    sizeof(*tpmi_sst->punit_info),
					    GFP_KERNEL);
	if (!tpmi_sst->punit_info)
		return -ENOMEM;

	tpmi_sst->number_of_punits = num_resources;

	for (i = 0; i < num_resources; ++i) {
		struct resource *res;

		res = tpmi_get_resource_at_index(auxdev, i);
		if (!res)
			continue;

		tpmi_sst->punit_info[i].package_id = pkg;
		tpmi_sst->punit_info[i].die_id = i;
		tpmi_sst->punit_info[i].auxdev= auxdev;
		tpmi_sst->punit_info[i].sst_base = devm_ioremap_resource(&auxdev->dev, res);
		if (IS_ERR(tpmi_sst->punit_info[i].sst_base))
			return PTR_ERR(tpmi_sst->punit_info[i].sst_base);

		ret = sst_main(auxdev, &tpmi_sst->punit_info[i]);
		if (ret) {
			dev_dbg(&auxdev->dev, "Invalid resource id at :%x \n", i);
			devm_iounmap(&auxdev->dev, tpmi_sst->punit_info[i].sst_base);
			tpmi_sst->punit_info[i].sst_base =  NULL;
			continue;
		}

		++inst;
	}

	if (!inst)
		return -ENODEV;

	tpmi_sst->pkg_id = pkg;
	auxiliary_set_drvdata(auxdev, tpmi_sst);

	mutex_lock(&iss_tpmi_dev_lock);
	sst_common.sst_inst[pkg] = tpmi_sst;
	mutex_unlock(&iss_tpmi_dev_lock);

	pm_runtime_enable(&auxdev->dev);
	pm_runtime_set_autosuspend_delay(&auxdev->dev, 2000);
	pm_runtime_use_autosuspend(&auxdev->dev);
	pm_runtime_put(&auxdev->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(tpmi_sst_dev_add);

void tpmi_sst_dev_remove(struct auxiliary_device *auxdev)
{
	struct tpmi_sst_struct *tpmi_sst = auxiliary_get_drvdata(auxdev);

	mutex_lock(&iss_tpmi_dev_lock);
	sst_common.sst_inst[tpmi_sst->pkg_id] = NULL;
	mutex_unlock(&iss_tpmi_dev_lock);

	pm_runtime_get_sync(&auxdev->dev);
	pm_runtime_put_noidle(&auxdev->dev);
	pm_runtime_disable(&auxdev->dev);
}
EXPORT_SYMBOL_GPL(tpmi_sst_dev_remove);

int tpmi_sst_init(void)
{
	struct isst_if_cmd_cb cb;
	int ret = 0;

	mutex_lock(&iss_tpmi_dev_lock);

	if (isst_core_usage_count) {
		++isst_core_usage_count;
		goto init_done;
	}

	memset(&cb, 0, sizeof(cb));
	cb.cmd_size = sizeof(struct isst_if_io_reg);
	cb.offset = offsetof(struct isst_if_io_regs, io_reg);
	cb.cmd_callback = NULL;
	cb.def_ioctl = isst_if_def_ioctl;
	cb.owner = THIS_MODULE;
	ret = isst_if_cdev_register(ISST_IF_DEV_TPMI, &cb);

init_done:
	mutex_unlock(&iss_tpmi_dev_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tpmi_sst_init);

void tpmi_sst_exit(void)
{
	mutex_lock(&iss_tpmi_dev_lock);
	if (isst_core_usage_count)
		--isst_core_usage_count;

	if (!isst_core_usage_count)
		isst_if_cdev_unregister(ISST_IF_DEV_TPMI);
	mutex_unlock(&iss_tpmi_dev_lock);
}
EXPORT_SYMBOL_GPL(tpmi_sst_exit);

MODULE_LICENSE("GPL v2");
