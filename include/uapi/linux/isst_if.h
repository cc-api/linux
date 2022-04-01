/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Intel Speed Select Interface: OS to hardware Interface
 * Copyright (c) 2019, Intel Corporation.
 * All rights reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */

#ifndef __ISST_IF_H
#define __ISST_IF_H

#include <linux/types.h>

/**
 * struct isst_if_platform_info - Define platform information
 * @api_version:	Version of the firmware document, which this driver
 *			can communicate
 * @driver_version:	Driver version, which will help user to send right
 *			commands. Even if the firmware is capable, driver may
 *			not be ready
 * @max_cmds_per_ioctl:	Returns the maximum number of commands driver will
 *			accept in a single ioctl
 * @mbox_supported:	Support of mail box interface
 * @mmio_supported:	Support of mmio interface for core-power feature
 *
 * Used to return output of IOCTL ISST_IF_GET_PLATFORM_INFO. This
 * information can be used by the user space, to get the driver, firmware
 * support and also number of commands to send in a single IOCTL request.
 */
struct isst_if_platform_info {
	__u16 api_version;
	__u16 driver_version;
	__u16 max_cmds_per_ioctl;
	__u8 mbox_supported;
	__u8 mmio_supported;
};

/**
 * struct isst_if_cpu_map - CPU mapping between logical and physical CPU
 * @logical_cpu:	Linux logical CPU number
 * @physical_cpu:	PUNIT CPU number
 *
 * Used to convert from Linux logical CPU to PUNIT CPU numbering scheme.
 * The PUNIT CPU number is different than APIC ID based CPU numbering.
 */
struct isst_if_cpu_map {
	__u32 logical_cpu;
	__u32 physical_cpu;
};

/**
 * struct isst_if_cpu_maps - structure for CPU map IOCTL
 * @cmd_count:	Number of CPU mapping command in cpu_map[]
 * @cpu_map[]:	Holds one or more CPU map data structure
 *
 * This structure used with ioctl ISST_IF_GET_PHY_ID to send
 * one or more CPU mapping commands. Here IOCTL return value indicates
 * number of commands sent or error number if no commands have been sent.
 */
struct isst_if_cpu_maps {
	__u32 cmd_count;
	struct isst_if_cpu_map cpu_map[1];
};

/**
 * struct isst_if_io_reg - Read write PUNIT IO register
 * @read_write:		Value 0: Read, 1: Write
 * @logical_cpu:	Logical CPU number to get target PCI device.
 * @reg:		PUNIT register offset
 * @value:		For write operation value to write and for
 *			read placeholder read value
 *
 * Structure to specify read/write data to PUNIT registers.
 */
struct isst_if_io_reg {
	__u32 read_write; /* Read:0, Write:1 */
	__u32 logical_cpu;
	__u32 reg;
	__u32 value;
};

/**
 * struct isst_if_io_regs - structure for IO register commands
 * @cmd_count:	Number of io reg commands in io_reg[]
 * @io_reg[]:	Holds one or more io_reg command structure
 *
 * This structure used with ioctl ISST_IF_IO_CMD to send
 * one or more read/write commands to PUNIT. Here IOCTL return value
 * indicates number of requests sent or error number if no requests have
 * been sent.
 */
struct isst_if_io_regs {
	__u32 req_count;
	struct isst_if_io_reg io_reg[1];
};

/**
 * struct isst_if_mbox_cmd - Structure to define mail box command
 * @logical_cpu:	Logical CPU number to get target PCI device
 * @parameter:		Mailbox parameter value
 * @req_data:		Request data for the mailbox
 * @resp_data:		Response data for mailbox command response
 * @command:		Mailbox command value
 * @sub_command:	Mailbox sub command value
 * @reserved:		Unused, set to 0
 *
 * Structure to specify mailbox command to be sent to PUNIT.
 */
struct isst_if_mbox_cmd {
	__u32 logical_cpu;
	__u32 parameter;
	__u32 req_data;
	__u32 resp_data;
	__u16 command;
	__u16 sub_command;
	__u32 reserved;
};

/**
 * struct isst_if_mbox_cmds - structure for mailbox commands
 * @cmd_count:	Number of mailbox commands in mbox_cmd[]
 * @mbox_cmd[]:	Holds one or more mbox commands
 *
 * This structure used with ioctl ISST_IF_MBOX_COMMAND to send
 * one or more mailbox commands to PUNIT. Here IOCTL return value
 * indicates number of commands sent or error number if no commands have
 * been sent.
 */
struct isst_if_mbox_cmds {
	__u32 cmd_count;
	struct isst_if_mbox_cmd mbox_cmd[1];
};

/**
 * struct isst_if_msr_cmd - Structure to define msr command
 * @read_write:		Value 0: Read, 1: Write
 * @logical_cpu:	Logical CPU number
 * @msr:		MSR number
 * @data:		For write operation, data to write, for read
 *			place holder
 *
 * Structure to specify MSR command related to PUNIT.
 */
struct isst_if_msr_cmd {
	__u32 read_write; /* Read:0, Write:1 */
	__u32 logical_cpu;
	__u64 msr;
	__u64 data;
};

/**
 * struct isst_if_msr_cmds - structure for msr commands
 * @cmd_count:	Number of mailbox commands in msr_cmd[]
 * @msr_cmd[]:	Holds one or more msr commands
 *
 * This structure used with ioctl ISST_IF_MSR_COMMAND to send
 * one or more MSR commands. IOCTL return value indicates number of
 * commands sent or error number if no commands have been sent.
 */
struct isst_if_msr_cmds {
	__u32 cmd_count;
	struct isst_if_msr_cmd msr_cmd[1];
};

/**
 * struct isst_core_power - Structure to get/set core_power feature
 * @get_set:	0: Get, 1: Set
 * @socket_id:	socket/package id
 * @die_id:	die id
 * @enable:	Feature enable status
 * @priority_type:	Priority type for the feature (ordered/proportional)
  *
 * Structure to get/set core_power feature.
 */
struct isst_core_power {
	__u8 get_set;
	__u8 socket_id;
	__u8 die_id;
	__u8 enable;
	__u8 supported;
	__u8 priority_type;
};

/**
 * struct isst_clos_param, - Structure to get/set clos praram
 * @get_set:	0: Get, 1: Set
 * @socket_id:	socket/package id
 * @die_id:	die id
  *
 * Structure to get/set core_power feature.
 */
struct isst_clos_param {
	__u8 get_set;
	__u8 socket_id;
	__u8 die_id;
	__u8 clos;
	__u16 min_freq_mhz;
	__u16 max_freq_mhz;
	__u8 prop_prio;
};

struct isst_if_clos_assoc {
	__u8 socket_id;
	__u8 die_id;
	__u16 logical_cpu;
	__u16 clos;
};

struct isst_if_clos_assoc_cmds {
	__u16 cmd_count;
	__u16 get_set;
	__u16 punit_cpu_map;
	struct isst_if_clos_assoc assoc_info[1];
};

struct isst_perf_level_info {
	__u8 socket_id;
	__u8 die_id;
	__u8 levels;
	__u8 feature_rev;
	__u8 level_mask;
	__u8 current_level;
	__u8 feature_state;
	__u8 locked;
	__u8 enabled;
	__u8 sst_tf_support;
	__u8 sst_bf_support;
};

struct isst_perf_level_control {
	__u8 socket_id;
	__u8 die_id;
	__u8 level;
};

struct isst_perf_feature_control {
	__u8 socket_id;
	__u8 die_id;
	__u8 feature;
};

struct isst_perf_level_data_info {
	__u8 socket_id;
	__u8 die_id;
	__u16 level;
	__u16 tdp_ratio;
	__u16 base_freq_mhz;
	__u16 base_freq_avx2_mhz;
	__u16 base_freq_avx512_mhz;
	__u16 base_freq_amx_mhz;
	__u16 thermal_design_power_w;
	__u16 tjunction_max_c;
	__u16 max_memory_freq_mhz;
	__u16 cooling_type;

	__u16 cdyn0_bucket0_freq_mhz;
	__u16 cdyn0_bucket1_freq_mhz;
	__u16 cdyn0_bucket2_freq_mhz;
	__u16 cdyn0_bucket3_freq_mhz;
	__u16 cdyn0_bucket4_freq_mhz;
	__u16 cdyn0_bucket5_freq_mhz;
	__u16 cdyn0_bucket6_freq_mhz;
	__u16 cdyn0_bucket7_freq_mhz;

	__u16 cdyn1_bucket0_freq_mhz;
	__u16 cdyn1_bucket1_freq_mhz;
	__u16 cdyn1_bucket2_freq_mhz;
	__u16 cdyn1_bucket3_freq_mhz;
	__u16 cdyn1_bucket4_freq_mhz;
	__u16 cdyn1_bucket5_freq_mhz;
	__u16 cdyn1_bucket6_freq_mhz;
	__u16 cdyn1_bucket7_freq_mhz;

	__u16 cdyn2_bucket0_freq_mhz;
	__u16 cdyn2_bucket1_freq_mhz;
	__u16 cdyn2_bucket2_freq_mhz;
	__u16 cdyn2_bucket3_freq_mhz;
	__u16 cdyn2_bucket4_freq_mhz;
	__u16 cdyn2_bucket5_freq_mhz;
	__u16 cdyn2_bucket6_freq_mhz;
	__u16 cdyn2_bucket7_freq_mhz;

	__u16 cdyn3_bucket0_freq_mhz;
	__u16 cdyn3_bucket1_freq_mhz;
	__u16 cdyn3_bucket2_freq_mhz;
	__u16 cdyn3_bucket3_freq_mhz;
	__u16 cdyn3_bucket4_freq_mhz;
	__u16 cdyn3_bucket5_freq_mhz;
	__u16 cdyn3_bucket6_freq_mhz;
	__u16 cdyn3_bucket7_freq_mhz;

	__u16 cdyn4_bucket0_freq_mhz;
	__u16 cdyn4_bucket1_freq_mhz;
	__u16 cdyn4_bucket2_freq_mhz;
	__u16 cdyn4_bucket3_freq_mhz;
	__u16 cdyn4_bucket4_freq_mhz;
	__u16 cdyn4_bucket5_freq_mhz;
	__u16 cdyn4_bucket6_freq_mhz;
	__u16 cdyn4_bucket7_freq_mhz;

	__u16 cdyn5_bucket0_freq_mhz;
	__u16 cdyn5_bucket1_freq_mhz;
	__u16 cdyn5_bucket2_freq_mhz;
	__u16 cdyn5_bucket3_freq_mhz;
	__u16 cdyn5_bucket4_freq_mhz;
	__u16 cdyn5_bucket5_freq_mhz;
	__u16 cdyn5_bucket6_freq_mhz;
	__u16 cdyn5_bucket7_freq_mhz;

	__u16 bucket0_core_count;
	__u16 bucket1_core_count;
	__u16 bucket2_core_count;
	__u16 bucket3_core_count;
	__u16 bucket4_core_count;
	__u16 bucket5_core_count;
	__u16 bucket6_core_count;
	__u16 bucket7_core_count;

	__u16 p0_core_ratio;
	__u16 p1_core_ratio;
	__u16 pn_core_ratio;
	__u16 pm_core_ratio;
	__u16 p0_fabric_ratio;
	__u16 p1_fabric_ratio;
	__u16 pn_fabric_ratio;
	__u16 pm_fabric_ratio;
};

struct isst_perf_level_cpu_mask {
	__u8 socket_id;
	__u8 die_id;
	__u8 level;
	__u8 punit_cpu_map;
	__u64 mask;
	__u16 cpu_count;
	__s16 cpus[128];
};

struct isst_base_freq_info {
	__u8 socket_id;
	__u8 die_id;
	__u16 level;
	__u16 high_base_freq_mhz;
	__u16 low_base_freq_mhz;
	__u16 tjunction_max_c;
	__u16 thermal_design_power_w;
};


struct isst_turbo_freq_info {
	__u8 socket_id;
	__u8 die_id;
	__u16 level;
	__u16 lp_clip_0_mhz;
	__u16 lp_clip_1_mhz;
	__u16 lp_clip_2_mhz;
	__u16 lp_clip_3_mhz;
	__u16 bucket_0_core_count;
	__u16 bucket_1_core_count;
	__u16 bucket_2_core_count;
	__u16 bucket_3_core_count;
	__u16 bucket_4_core_count;
	__u16 bucket_5_core_count;
	__u16 bucket_6_core_count;
	__u16 bucket_7_core_count;
	__u16 bucket_0_cydn_level_0_trl;
	__u16 bucket_1_cydn_level_0_trl;
	__u16 bucket_2_cydn_level_0_trl;
	__u16 bucket_3_cydn_level_0_trl;
	__u16 bucket_4_cydn_level_0_trl;
	__u16 bucket_5_cydn_level_0_trl;
	__u16 bucket_6_cydn_level_0_trl;
	__u16 bucket_7_cydn_level_0_trl;
	__u16 bucket_0_cydn_level_1_trl;
	__u16 bucket_1_cydn_level_1_trl;
	__u16 bucket_2_cydn_level_1_trl;
	__u16 bucket_3_cydn_level_1_trl;
	__u16 bucket_4_cydn_level_1_trl;
	__u16 bucket_5_cydn_level_1_trl;
	__u16 bucket_6_cydn_level_1_trl;
	__u16 bucket_7_cydn_level_1_trl;
	__u16 bucket_0_cydn_level_2_trl;
	__u16 bucket_1_cydn_level_2_trl;
	__u16 bucket_2_cydn_level_2_trl;
	__u16 bucket_3_cydn_level_2_trl;
	__u16 bucket_4_cydn_level_2_trl;
	__u16 bucket_5_cydn_level_2_trl;
	__u16 bucket_6_cydn_level_2_trl;
	__u16 bucket_7_cydn_level_2_trl;
	__u16 bucket_0_cydn_level_3_trl;
	__u16 bucket_1_cydn_level_3_trl;
	__u16 bucket_2_cydn_level_3_trl;
	__u16 bucket_3_cydn_level_3_trl;
	__u16 bucket_4_cydn_level_3_trl;
	__u16 bucket_5_cydn_level_3_trl;
	__u16 bucket_6_cydn_level_3_trl;
	__u16 bucket_7_cydn_level_3_trl;
	__u16 bucket_0_cydn_level_4_trl;
	__u16 bucket_1_cydn_level_4_trl;
	__u16 bucket_2_cydn_level_4_trl;
	__u16 bucket_3_cydn_level_4_trl;
	__u16 bucket_4_cydn_level_4_trl;
	__u16 bucket_5_cydn_level_4_trl;
	__u16 bucket_6_cydn_level_4_trl;
	__u16 bucket_7_cydn_level_4_trl;
	__u16 bucket_0_cydn_level_5_trl;
	__u16 bucket_1_cydn_level_5_trl;
	__u16 bucket_2_cydn_level_5_trl;
	__u16 bucket_3_cydn_level_5_trl;
	__u16 bucket_4_cydn_level_5_trl;
	__u16 bucket_5_cydn_level_5_trl;
	__u16 bucket_6_cydn_level_5_trl;
	__u16 bucket_7_cydn_level_5_trl;
};

struct isst_tpmi_instance_count {
	__u8 socket_id;
	__u8 count;
	__u16 valid_mask;
};

#define ISST_IF_MAGIC			0xFE
#define ISST_IF_GET_PLATFORM_INFO	_IOR(ISST_IF_MAGIC, 0, struct isst_if_platform_info *)
#define ISST_IF_GET_PHY_ID		_IOWR(ISST_IF_MAGIC, 1, struct isst_if_cpu_map *)
#define ISST_IF_IO_CMD		_IOW(ISST_IF_MAGIC, 2, struct isst_if_io_regs *)
#define ISST_IF_MBOX_COMMAND	_IOWR(ISST_IF_MAGIC, 3, struct isst_if_mbox_cmds *)
#define ISST_IF_MSR_COMMAND	_IOWR(ISST_IF_MAGIC, 4, struct isst_if_msr_cmds *)

#define ISST_IF_CORE_POWER_STATE _IOWR(ISST_IF_MAGIC, 5, struct isst_core_power *)
#define ISST_IF_CLOS_PARAM	_IOWR(ISST_IF_MAGIC, 6, struct isst_clos_param *)
#define ISST_IF_CLOS_ASSOC	_IOWR(ISST_IF_MAGIC, 7, struct isst_if_clos_assoc_cmds *)

#define ISST_IF_PERF_LEVELS	_IOWR(ISST_IF_MAGIC, 8, struct isst_perf_level_info *)
#define ISST_IF_PERF_SET_LEVEL	_IOW(ISST_IF_MAGIC, 9, struct isst_perf_level_control *)
#define ISST_IF_PERF_SET_FEATURE _IOW(ISST_IF_MAGIC, 10, struct isst_perf_feature_control *)
#define ISST_IF_GET_PERF_LEVEL_INFO	_IOR(ISST_IF_MAGIC, 11, struct isst_perf_level_data_info *)
#define ISST_IF_GET_PERF_LEVEL_CPU_MASK	_IOR(ISST_IF_MAGIC, 12, struct isst_perf_level_cpu_mask *)
#define ISST_IF_GET_BASE_FREQ_INFO	_IOR(ISST_IF_MAGIC, 13, struct isst_base_freq_info *)
#define ISST_IF_GET_BASE_FREQ_CPU_MASK	_IOR(ISST_IF_MAGIC, 14, struct isst_perf_level_cpu_mask *)
#define ISST_IF_GET_TURBO_FREQ_INFO	_IOR(ISST_IF_MAGIC, 15, struct isst_turbo_freq_info *)
#define ISST_IF_COUNT_TPMI_INSTANCES	_IOR(ISST_IF_MAGIC, 16, struct isst_tpmi_instance_count *)

#endif
