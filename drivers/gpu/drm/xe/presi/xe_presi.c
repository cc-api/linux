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
}
