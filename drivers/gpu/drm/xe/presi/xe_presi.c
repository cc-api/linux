// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/timer.h>
#include <drm/drm_vblank.h>

#include "regs/xe_guc_regs.h"
#include "xe_device_types.h"
#include "xe_mmio.h"
#include "xe_presi.h"

static const char * const presi_mode_names[] = {
	[XE_PRESI_MODE_NONE] = "none (silicon)",
	[XE_PRESI_MODE_SIMULATOR] = "simulation",
	[XE_PRESI_MODE_EMULATOR_PIPEGT] = "emulation pipeGT",
	[XE_PRESI_MODE_EMULATOR_PIPE2D] = "emulation pipe2D",
};

static int xe_presi_mode = 0;
module_param_named_unsafe(presi_mode, xe_presi_mode, int, 0600);
MODULE_PARM_DESC(presi_mode, "Select pre-si mode "
		 "(0=none/silicon [default], 1=simulator,"
		 "2=pipeGT emulator, 3=pipe2D emulator)");

static int xe_presi_timeout_multiplier = 0;
module_param_named_unsafe(presi_timeout_multiplier,
			  xe_presi_timeout_multiplier, int, 0600);
MODULE_PARM_DESC(presi_timeout_multiplier,
		 "Timeout multiplier for presilicon execution");

static int xe_presi_disable_uc_auth = -1;
module_param_named_unsafe(disable_uc_auth, xe_presi_disable_uc_auth, int, 0400);
MODULE_PARM_DESC(disable_uc_auth, "Disable uc authentication (0=enable authentication [default], 1=disable authentication");

#define XE_PRESI_FORCE_DISABLE_FEATURE(xe, name) \
	xe->presi_info.disabled_features |= XE_PRESI_FEATURE_BIT(name)

#define XE_PRESI_FORCE_ENABLE_FEATURE(xe, name) \
	xe->presi_info.disabled_features &= ~XE_PRESI_FEATURE_BIT(name)

static void dg2_sim_init_disabled_features(struct xe_device *xe)
{
	xe->presi_info.disabled_features = 0;
}

static void pvc_sim_init_disabled_features(struct xe_device *xe)
{
	xe->presi_info.disabled_features = \
					   XE_PRESI_FEATURE_BIT(GUC_RESET)| \
					   XE_PRESI_FEATURE_BIT(UC_AUTH)| \
					   XE_PRESI_FEATURE_BIT(GUC_SLPC);
}

/*
 * For now there is no common feature which is disabled across all platforms on
 * simulator environment.
 * This would avoid adding new switch cases for platforms if they just disable
 * the feature which is common for all platforms.
 */
#define XE_PRESI_SIM_COMMON_DISABLED_FEATURES 0

static void xe_presi_init_disabled_features(struct xe_device *xe)
{
	BUILD_BUG_ON(sizeof(xe->presi_info.disabled_features) * BITS_PER_BYTE <
		     XE_PRESI_FEATURE_COUNT);

	if (IS_SIMULATOR(xe)) {
		xe->presi_info.disabled_features =
			XE_PRESI_SIM_COMMON_DISABLED_FEATURES;
		switch(xe->info.platform) {
			case XE_DG2:
				dg2_sim_init_disabled_features(xe);
				break;
			case XE_PVC:
				pvc_sim_init_disabled_features(xe);
				break;
			default:
				; /* Added just to satisfy the warning */
		}
	}
	/* Other presilicon environments like PipeGT and Pipe2D are yet to be handled */
}

/**
 * xe_presi_init - checks the pre-si modparam and acts on it
 * @xe:	xe device
 *
 * presi_mode is only updated if the modparam is set to a valid value. An
 * error is logged if the modparam is set incorrectly
 */
void xe_presi_init(struct xe_device *xe)
{
	enum xe_presi_mode mode = MODPARAM_TO_PRESI_MODE(xe_presi_mode);

	BUILD_BUG_ON(XE_PRESI_MODE_UNKNOWN); /* unknown needs to be 0 */
	XE_WARN_ON(xe->presi_info.mode != XE_PRESI_MODE_UNKNOWN);

	if (mode > XE_PRESI_MODE_NONE && mode <= XE_MAX_PRESI_MODE) {
		drm_info(&xe->drm, "using pre-silicon mode from modparam: %s\n",
				 presi_mode_names[mode]);
		xe->presi_info.mode = mode;
	} else {
		if (mode != XE_PRESI_MODE_NONE)
			DRM_ERROR("invalid pre-silicon mode %d selected in modparam! defaulting to silicon mode\n",
				  xe_presi_mode);
		xe->presi_info.mode = XE_PRESI_MODE_NONE;
	}

	xe_presi_init_disabled_features(xe);

	xe->presi_info.timeout_multiplier = 1;
	if (xe_presi_timeout_multiplier) {
		xe->presi_info.timeout_multiplier = xe_presi_timeout_multiplier;
	} else {
		/* presilicon timeout multiplier module param is not set. */
		if (IS_PRESILICON(xe))
			xe->presi_info.timeout_multiplier = 100;
	}

	if (xe->presi_info.timeout_multiplier > 1)
		drm_info(&xe->drm, "using pre-silicon timeout multiplier: %d\n",
				xe->presi_info.timeout_multiplier);

	/* disable_uc_auth param is set */
	if (xe_presi_disable_uc_auth > -1) {
		if (xe_presi_disable_uc_auth) {
			XE_PRESI_FORCE_DISABLE_FEATURE(xe, UC_AUTH);
		} else {
			XE_PRESI_FORCE_ENABLE_FEATURE(xe, UC_AUTH);
		}
		drm_info(&xe->drm, "uC authentication: %s\n",
			 (xe_presi_disable_uc_auth) ? "disabled" : "enabled");
	}
}

#define GUC_SHIM_CONTROL2_VALUE (GUC_SHA_COMPUTATION_DISABLE	| \
				 GUC_RSA_CHECK_BOOT_ROM_DISABLE	| \
				 GUC_RSA_KEY_SELECTION)

void xe_presi_skip_uc_auth(struct xe_gt *gt)
{
	struct xe_device *xe =  gt_to_xe(gt);
	/*
	 * uc firmware authentication could be disabled using module parameter or
	 * when executing on presilicon environment
	 */
	if (XE_PRESI_SKIP_FEATURE(xe, UC_AUTH))
		xe_mmio_write32(gt, GUC_SHIM_CONTROL2, GUC_SHIM_CONTROL2_VALUE);
}

bool xe_presi_setup_guc_wopcm_region(struct xe_gt *gt, u32 *guc_wopcm_base,
				     u32 *guc_wopcm_size)
{
	u32 mask;
	int err;
	u32 guc_base, guc_size;

	/*
	 * These values are chosen based on tests using PVC simulation.
	 */
	guc_base = 0x4000;
	guc_size = 0x100000;

	mask = GUC_WOPCM_OFFSET_MASK | GUC_WOPCM_OFFSET_VALID |
	       HUC_LOADING_AGENT_GUC;
	err = xe_mmio_write32_and_verify(gt, DMA_GUC_WOPCM_OFFSET,
					 guc_base | HUC_LOADING_AGENT_GUC,
					 mask, guc_base | HUC_LOADING_AGENT_GUC |
					 GUC_WOPCM_OFFSET_VALID);
	if (err) {
		drm_err(&gt_to_xe(gt)->drm, "Failed to write the GuC wopcm base to register, Offset:0x%X\n",
			guc_base);
		return false;
	}

	mask = GUC_WOPCM_SIZE_MASK | GUC_WOPCM_SIZE_LOCKED;
	err = xe_mmio_write32_and_verify(gt, GUC_WOPCM_SIZE, guc_size,
					 mask, guc_size | GUC_WOPCM_SIZE_LOCKED);
	if (err) {
		drm_err(&gt_to_xe(gt)->drm, "Failed to write the GuC wopcm size to register, size:0x%X\n",
			guc_size);
		return false;
	}

	*guc_wopcm_base = guc_base;
	*guc_wopcm_size = guc_size;

	return true;
}

#define XE_PRESI_TIMER_INTERVAL_MSECS 30

#if IS_ENABLED(CONFIG_DRM_XE_DISPLAY)
#include "i915_drv.h"
#include "intel_display.h"

static void xe_presi_irq_timer(struct timer_list *t)
{
	struct xe_device *xe = from_timer(xe, t, presi_info.irq_timer);
	struct drm_device *drm_dev = &xe->drm;
	int pipe;

	for_each_pipe(xe, pipe)
		if (pipe < drm_dev->num_crtcs)
			drm_handle_vblank(drm_dev, pipe);

	mod_timer(&xe->presi_info.irq_timer,
		  jiffies + msecs_to_jiffies(XE_PRESI_TIMER_INTERVAL_MSECS));
}

void xe_presi_irq_timer_start(struct xe_device *xe)
{
	if (!IS_SIMULATOR(xe))
		return;

	timer_setup(&xe->presi_info.irq_timer, xe_presi_irq_timer, 0);
	mod_timer(&xe->presi_info.irq_timer,
		  jiffies + msecs_to_jiffies(XE_PRESI_TIMER_INTERVAL_MSECS));
}

void xe_presi_irq_timer_stop(struct xe_device *xe)
{
	if (!IS_SIMULATOR(xe))
		return;

	del_timer_sync(&xe->presi_info.irq_timer);
}
#endif
