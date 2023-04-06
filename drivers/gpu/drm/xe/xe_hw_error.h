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
