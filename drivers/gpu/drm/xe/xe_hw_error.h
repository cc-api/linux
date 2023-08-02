/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */
#ifndef XE_HW_ERRORS_H_
#define XE_HW_ERRORS_H_

#include <linux/stddef.h>
#include <linux/types.h>

#define XE_RAS_REG_SIZE 32

/* Error categories reported by hardware */
enum hardware_error {
	HARDWARE_ERROR_CORRECTABLE = 0,
	HARDWARE_ERROR_NONFATAL = 1,
	HARDWARE_ERROR_FATAL = 2,
	HARDWARE_ERROR_MAX,
};

/* Count of Correctable and Uncorrectable errors reported on tile */
enum xe_tile_hw_errors {
	XE_HW_ERR_TILE_UNSPEC = 0,
	XE_HW_ERR_TILE_FATAL_SGGI,
	XE_HW_ERR_TILE_FATAL_SGLI,
	XE_HW_ERR_TILE_FATAL_SGUNIT,
	XE_HW_ERR_TILE_FATAL_SGCI,
	XE_HW_ERR_TILE_FATAL_MERT,
	XE_HW_ERR_TILE_FATAL_UNKNOWN,
	XE_HW_ERR_TILE_NONFATAL_SGGI,
	XE_HW_ERR_TILE_NONFATAL_SGLI,
	XE_HW_ERR_TILE_NONFATAL_SGUNIT,
	XE_HW_ERR_TILE_NONFATAL_SGCI,
	XE_HW_ERR_TILE_NONFATAL_MERT,
	XE_HW_ERR_TILE_NONFATAL_UNKNOWN,
	XE_HW_ERR_TILE_CORR_SGUNIT,
	XE_HW_ERR_TILE_CORR_UNKNOWN,
};

/* Count of GT Correctable and FATAL HW ERRORS */
enum xe_gt_hw_errors {
	XE_HW_ERR_GT_CORR_L3_SNG,
	XE_HW_ERR_GT_CORR_GUC,
	XE_HW_ERR_GT_CORR_SAMPLER,
	XE_HW_ERR_GT_CORR_SLM,
	XE_HW_ERR_GT_CORR_EU_IC,
	XE_HW_ERR_GT_CORR_EU_GRF,
	XE_HW_ERR_GT_CORR_UNKNOWN,
	XE_HW_ERR_GT_FATAL_ARR_BIST,
	XE_HW_ERR_GT_FATAL_FPU,
	XE_HW_ERR_GT_FATAL_L3_DOUB,
	XE_HW_ERR_GT_FATAL_L3_ECC_CHK,
	XE_HW_ERR_GT_FATAL_GUC,
	XE_HW_ERR_GT_FATAL_IDI_PAR,
	XE_HW_ERR_GT_FATAL_SQIDI,
	XE_HW_ERR_GT_FATAL_SAMPLER,
	XE_HW_ERR_GT_FATAL_SLM,
	XE_HW_ERR_GT_FATAL_EU_IC,
	XE_HW_ERR_GT_FATAL_EU_GRF,
	XE_HW_ERR_GT_FATAL_UNKNOWN,
};

struct err_name_index_pair {
	const char *name;
	const u32 index;
};

struct xe_device;
struct xe_tile;

void xe_hw_error_irq_handler(struct xe_tile *tile, const u32 master_ctl);
void xe_assign_hw_err_regs(struct xe_device *xe);
void xe_process_hw_errors(struct xe_device *xe);

#endif
