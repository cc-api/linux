// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_PRESI_H_
#define _XE_PRESI_H_

#include "xe_macros.h"
#include "xe_gt_types.h"

struct xe_device;
struct xe_vm;

#define XE_PRESI_FEATURE_LIST(macro) \
		macro(GUC_RESET), \
		macro(PCODE), \
		macro(UC_AUTH), \
		macro(GUC_SLPC), \
		macro(SAGV),

#define XE_PRESI_FEATURE_ENUM(name) XE_PRESI_FEATURE_EN_##name
enum xe_presi_feature {
	XE_PRESI_FEATURE_LIST(XE_PRESI_FEATURE_ENUM)
	XE_PRESI_FEATURE_COUNT
};

#define XE_PRESI_FEATURE_BIT(name) BIT(XE_PRESI_FEATURE_ENUM(name))

struct xe_presi_ops {
	int (*features_init)(struct xe_device *xe);
	int (*device_init)(struct xe_device *xe);
	void (*device_fini)(struct xe_device *xe);
	int (*vm_create)(struct xe_vm *vm);
	void (*vm_destroy)(struct xe_vm *vm);
};

/*
 * We support different pre-silicon modes:
 * - simulation: GPU is simulated. Model is functionally accurate but
 * 		 implementation does not necessarily match HW.
 * - emulation pipeGT: GT RTL is booted on FPGA, while the rest of the HW
 * 		       is simulated.
 * - emulation pipe2D: Display and Gunit RTL is booted on FPGA, while the rest
 * 		       of the HW is simulated.
 * - emulation pldm: HW is emulated on a PLDM or similar system.
 *
 * Note: the enum values for detected envs are equal to the modparam values + 1
 */
struct xe_presi_info {
	enum xe_presi_mode {
		XE_PRESI_MODE_UNKNOWN = 0, /* aka not detected yet */
		XE_PRESI_MODE_NONE = 1, /* aka SILICON */
		XE_PRESI_MODE_SIMULATOR = 2,
		XE_PRESI_MODE_EMULATOR_PIPEGT = 3,
		XE_PRESI_MODE_EMULATOR_PIPE2D = 4,
		XE_PRESI_MODE_EMULATOR_PLDM = 5,
		XE_NUM_PRESI_MODES
	} mode;

	struct xe_presi_ops *ops;
	u64 disabled_features;
	int timeout_multiplier;

	struct timer_list irq_timer; /* Timer to fake periordic interrupt */

	/* features used in presi phase */
	void *presi_features;
};

struct xe_presi_vm_info {
	/* VM features used in presi phase */
	void *vm_features;
};

#define MODPARAM_TO_PRESI_MODE(x) ({ \
	int val__ = (x); \
	val__ >= 0 ? val__ + 1 : val__; \
})

#define IS_PRESI_MODE(xe, x) ({ \
	XE_WARN_ON(xe->presi_info.mode == XE_PRESI_MODE_UNKNOWN); \
	xe->presi_info.mode == XE_PRESI_MODE_##x; \
})

#define IS_PRESILICON(xe) (!IS_PRESI_MODE(xe, NONE))
#define IS_SIMULATOR(xe) (IS_PRESI_MODE(xe, SIMULATOR))
#define IS_PIPEGT_EMULATOR(xe) (IS_PRESI_MODE(xe, EMULATOR_PIPEGT))
#define IS_PIPE2D_EMULATOR(xe) (IS_PRESI_MODE(xe, EMULATOR_PIPE2D))
#define IS_PLDM_EMULATOR(xe) (IS_PRESI_MODE(xe, EMULATOR_PLDM))

#define XE_PRESI_SKIP_FEATURE(xe, name) \
	(xe->presi_info.disabled_features & XE_PRESI_FEATURE_BIT(name))

#define XE_PRESI_TIMEOUT_MULTIPLIER(xe) (IS_PRESILICON(xe) ? \
					 xe->presi_info.timeout_multiplier : 1)

int xe_presi_device_init(struct xe_device *xe);
int xe_presi_init(struct xe_device *xe);
int xe_presi_vm_create(struct xe_vm *vm);
void xe_presi_vm_destroy(struct xe_vm *vm);

void xe_presi_skip_uc_auth(struct xe_gt *gt);
bool xe_presi_setup_guc_wopcm_region(struct xe_gt *gt, u32 *wopcm_base,
				     u32 *wopcm_size);

#ifdef CONFIG_DRM_XE_FS1
struct xe_presi_ops *xe_fs1_presi_get_ops(void);
#endif

#endif
