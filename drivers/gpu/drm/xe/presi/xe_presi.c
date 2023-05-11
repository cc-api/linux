// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_device_types.h"
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

#define XE_PRESI_FORCE_DISABLE_FEATURE(xe, name) \
	xe->presi_info.disabled_features |= XE_PRESI_FEATURE_BIT(name)

#define XE_PRESI_FORCE_ENABLE_FEATURE(xe, name) \
	xe->presi_info.disabled_features &= ~XE_PRESI_FEATURE_BIT(name)

static void dg2_sim_init_disabled_features(struct xe_device *xe)
{
	xe->presi_info.disabled_features = 0;
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
			default:
				/* Added just to satisfy the warning */
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
	XE_BUG_ON(xe->presi_info.mode != XE_PRESI_MODE_UNKNOWN);

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
}
