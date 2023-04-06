// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_hw_error.h"

#include "regs/xe_regs.h"
#include "regs/xe_tile_error_regs.h"
#include "xe_device.h"
#include "xe_mmio.h"

static const char *
hardware_error_type_to_str(const enum hardware_error hw_err)
{
	switch (hw_err) {
	case HARDWARE_ERROR_CORRECTABLE:
		return "CORRECTABLE";
	case HARDWARE_ERROR_NONFATAL:
		return "NONFATAL";
	case HARDWARE_ERROR_FATAL:
		return "FATAL";
	default:
		return "UNKNOWN";
	}
}

static const struct err_name_index_pair dg2_err_stat_fatal_reg[] = {
	[0]         = {"GT",			XE_HW_ERR_TILE_UNSPEC},
	[1 ... 3]   = {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[4 ... 7]   = {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[8]         = {"GSC",			XE_HW_ERR_TILE_UNSPEC},
	[9 ... 11]  = {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[12]        = {"SGUNIT",		XE_HW_ERR_TILE_FATAL_SGUNIT},
	[13 ... 15] = {"Undefined",             XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[16]        = {"SOC",			XE_HW_ERR_TILE_UNSPEC},
	[17 ... 31] = {"Undefined",             XE_HW_ERR_TILE_FATAL_UNKNOWN},
};

static const struct err_name_index_pair dg2_err_stat_nonfatal_reg[] = {
	[0]         = {"GT",			XE_HW_ERR_TILE_UNSPEC},
	[1 ... 3]   = {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[4 ... 7]   = {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[8]         = {"GSC",			XE_HW_ERR_TILE_UNSPEC},
	[9 ... 11]  = {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[12]        = {"SGUNIT",		XE_HW_ERR_TILE_NONFATAL_SGUNIT},
	[13 ... 15] = {"Undefined",             XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[16]        = {"SOC",			XE_HW_ERR_TILE_UNSPEC},
	[17 ... 19] = {"Undefined",             XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[20]        = {"MERT",			XE_HW_ERR_TILE_NONFATAL_MERT},
	[21 ... 31] = {"Undefined",             XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
};

static const struct err_name_index_pair dg2_err_stat_correctable_reg[] = {
	[0]         = {"GT",			XE_HW_ERR_TILE_UNSPEC},
	[1 ... 3]   = {"Undefined",		XE_HW_ERR_TILE_CORR_UNKNOWN},
	[4 ... 7]   = {"Undefined",		XE_HW_ERR_TILE_CORR_UNKNOWN},
	[8]         = {"GSC",			XE_HW_ERR_TILE_UNSPEC},
	[9 ... 11]  = {"Undefined",		XE_HW_ERR_TILE_CORR_UNKNOWN},
	[12]        = {"SGUNIT",		XE_HW_ERR_TILE_CORR_SGUNIT},
	[13 ... 15] = {"Undefined",             XE_HW_ERR_TILE_CORR_UNKNOWN},
	[16]        = {"SOC",			XE_HW_ERR_TILE_UNSPEC},
	[17 ... 31] = {"Undefined",             XE_HW_ERR_TILE_CORR_UNKNOWN},
};

static const struct err_name_index_pair pvc_err_stat_fatal_reg[] = {
	[0]         =  {"GT",			XE_HW_ERR_TILE_UNSPEC},
	[1]         =  {"SGGI Cmd Parity",	XE_HW_ERR_TILE_FATAL_SGGI},
	[2 ... 7]   =  {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[8]         =  {"GSC",			XE_HW_ERR_TILE_UNSPEC},
	[9]         =  {"SGLI Cmd Parity",	XE_HW_ERR_TILE_FATAL_SGLI},
	[10 ... 12] =  {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[13]        =  {"SGCI Cmd Parity",	XE_HW_ERR_TILE_FATAL_SGCI},
	[14 ... 15] =  {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[16]        =  {"SOC ERROR",		XE_HW_ERR_TILE_UNSPEC},
	[17 ... 19] =  {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[20]        =  {"MERT Cmd Parity",	XE_HW_ERR_TILE_FATAL_MERT},
	[21 ... 31] =  {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_err_stat_nonfatal_reg[] = {
	[0]         =  {"GT",			XE_HW_ERR_TILE_UNSPEC},
	[1]         =  {"SGGI Data Parity",	XE_HW_ERR_TILE_NONFATAL_SGGI},
	[2 ... 7]   =  {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[8]         =  {"GSC",			XE_HW_ERR_TILE_UNSPEC},
	[9]         =  {"SGLI Data Parity",	XE_HW_ERR_TILE_NONFATAL_SGLI},
	[10 ... 12] =  {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[13]        =  {"SGCI Data Parity",	XE_HW_ERR_TILE_NONFATAL_SGCI},
	[14 ... 15] =  {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[16]        =  {"SOC",			XE_HW_ERR_TILE_UNSPEC},
	[17 ... 19] =  {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[20]        =  {"MERT Data Parity",	XE_HW_ERR_TILE_NONFATAL_MERT},
	[21 ... 31] =  {"Undefined",            XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_err_stat_correctable_reg[] = {
	[0]         =  {"GT",			XE_HW_ERR_TILE_UNSPEC},
	[1 ... 7]   =  {"Undefined",		XE_HW_ERR_TILE_CORR_UNKNOWN},
	[8]         =  {"GSC",			XE_HW_ERR_TILE_UNSPEC},
	[9 ... 31]  =  {"Undefined",		XE_HW_ERR_TILE_CORR_UNKNOWN},
};

void xe_assign_hw_err_regs(struct xe_device *xe)
{
	const struct err_name_index_pair **dev_err_stat = xe->hw_err_regs.dev_err_stat;

	/* Error reporting is supported only for DG2 and PVC currently. */
	if (xe->info.platform == XE_DG2) {
		dev_err_stat[HARDWARE_ERROR_CORRECTABLE] = dg2_err_stat_correctable_reg;
		dev_err_stat[HARDWARE_ERROR_NONFATAL] = dg2_err_stat_nonfatal_reg;
		dev_err_stat[HARDWARE_ERROR_FATAL] = dg2_err_stat_fatal_reg;
	}

	if (xe->info.platform == XE_PVC) {
		dev_err_stat[HARDWARE_ERROR_CORRECTABLE] = pvc_err_stat_correctable_reg;
		dev_err_stat[HARDWARE_ERROR_NONFATAL] = pvc_err_stat_nonfatal_reg;
		dev_err_stat[HARDWARE_ERROR_FATAL] = pvc_err_stat_fatal_reg;
	}
}

static bool xe_platform_has_ras(struct xe_device *xe)
{
	if (xe->info.platform == XE_PVC || xe->info.platform == XE_DG2)
		return true;

	return false;
}

static void
xe_update_hw_error_cnt(struct drm_device *drm, struct xarray *hw_error, unsigned long index)
{
	unsigned long flags;
	void *entry;

	entry = xa_load(hw_error, index);
	entry = xa_mk_value(xa_to_value(entry) + 1);

	xa_lock_irqsave(hw_error, flags);
	if (xa_is_err(__xa_store(hw_error, index, entry, GFP_ATOMIC)))
		drm_err_ratelimited(drm,
				    HW_ERR "Error reported by index %ld is lost\n", index);
	xa_unlock_irqrestore(hw_error, flags);
}

static void
xe_hw_error_source_handler(struct xe_tile *tile, const enum hardware_error hw_err)
{
	const char *hw_err_str = hardware_error_type_to_str(hw_err);
	const struct hardware_errors_regs *err_regs;
	const struct err_name_index_pair *errstat;
	unsigned long errsrc;
	unsigned long flags;
	const char *name;
	struct xe_gt *gt;
	u32 indx;
	u32 errbit;

	if (!xe_platform_has_ras(tile_to_xe(tile)))
		return;

	spin_lock_irqsave(&tile_to_xe(tile)->irq.lock, flags);
	err_regs = &tile_to_xe(tile)->hw_err_regs;
	errstat = err_regs->dev_err_stat[hw_err];
	gt = tile->primary_gt;
	errsrc = xe_mmio_read32(gt, DEV_ERR_STAT_REG(hw_err));
	if (!errsrc) {
		drm_err_ratelimited(&tile_to_xe(tile)->drm, HW_ERR
				    "TILE%d reported DEV_ERR_STAT_REG_%s blank!\n",
				    tile->id, hw_err_str);
		goto unlock;
	}

	if (tile_to_xe(tile)->info.platform != XE_DG2)
		drm_dbg(&tile_to_xe(tile)->drm, HW_ERR
			"TILE%d reported DEV_ERR_STAT_REG_%s=0x%08lx\n",
			tile->id, hw_err_str, errsrc);

	for_each_set_bit(errbit, &errsrc, XE_RAS_REG_SIZE) {
		name = errstat[errbit].name;
		indx = errstat[errbit].index;

		if (hw_err == HARDWARE_ERROR_CORRECTABLE &&
		    tile_to_xe(tile)->info.platform != XE_DG2)
			drm_warn(&tile_to_xe(tile)->drm,
				 HW_ERR "TILE%d reported %s %s error, bit[%d] is set\n",
				 tile->id, name, hw_err_str, errbit);

		else if (tile_to_xe(tile)->info.platform != XE_DG2)
			drm_err_ratelimited(&tile_to_xe(tile)->drm,
					    HW_ERR "TILE%d reported %s %s error, bit[%d] is set\n",
					    tile->id, name, hw_err_str, errbit);

		if (indx != XE_HW_ERR_TILE_UNSPEC)
			xe_update_hw_error_cnt(&tile_to_xe(tile)->drm,
					       &tile->errors.hw_error, indx);
	}

	xe_mmio_write32(gt, DEV_ERR_STAT_REG(hw_err), errsrc);
unlock:
	spin_unlock_irqrestore(&tile_to_xe(tile)->irq.lock, flags);
}

/*
 * XE Platforms adds three Error bits to the Master Interrupt
 * Register to support error handling. These three bits are
 * used to convey the class of error:
 * FATAL, NONFATAL, or CORRECTABLE.
 *
 * To process an interrupt:
 *       Determine source of error (IP block) by reading
 *	 the Device Error Source Register (RW1C) that
 *	 corresponds to the class of error being serviced
 *	 and log the error.
 */
void
xe_hw_error_irq_handler(struct xe_tile *tile, const u32 master_ctl)
{
	enum hardware_error hw_err;

	for (hw_err = 0; hw_err < HARDWARE_ERROR_MAX; hw_err++) {
		if (master_ctl & XE_ERROR_IRQ(hw_err))
			xe_hw_error_source_handler(tile, hw_err);
	}
}

/*
 * xe_process_hw_errors - checks for the occurrence of HW errors
 *
 * Fatal will result in a card warm reset and driver will be reloaded.
 * This checks for the HW Errors that might have occurred in the
 * previous boot of the driver.
 */
void xe_process_hw_errors(struct xe_device *xe)
{
	struct xe_tile *root_tile = xe_device_get_root_tile(xe);
	struct xe_gt *root_gt = root_tile->primary_gt;

	u32 dev_pcieerr_status, master_ctl;
	struct xe_tile *tile;
	int i;

	dev_pcieerr_status = xe_mmio_read32(root_gt, DEV_PCIEERR_STATUS);

	for_each_tile(tile, xe, i) {
		struct xe_gt *gt = tile->primary_gt;

		if (dev_pcieerr_status & DEV_PCIEERR_IS_FATAL(i))
			xe_hw_error_source_handler(tile, HARDWARE_ERROR_FATAL);

		master_ctl = xe_mmio_read32(gt, GFX_MSTR_IRQ);
		xe_hw_error_irq_handler(tile, master_ctl);
		xe_mmio_write32(gt, GFX_MSTR_IRQ, master_ctl);
	}
	if (dev_pcieerr_status)
		xe_mmio_write32(root_gt, DEV_PCIEERR_STATUS, dev_pcieerr_status);
}
