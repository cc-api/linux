// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "xe_pat.h"

#include "regs/xe_reg_defs.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_mcr.h"
#include "xe_mmio.h"

#define _PAT_ATS				0x47fc
#define _PAT_INDEX(index)			_PICK_EVEN_2RANGES(index, 8, \
								   0x4800, 0x4804, \
								   0x4848, 0x484c)
#define _PAT_PTA				0x4820

#define XE2_NO_PROMOTE                          REG_BIT(10)
#define XE2_COMP_EN                             REG_BIT(9)
#define XE2_L3_CLOS                             REG_GENMASK(7, 6)
#define XE2_L3_POLICY                           REG_GENMASK(5, 4)
#define XE2_L4_POLICY                           REG_GENMASK(3, 2)
#define XE2_COH_MODE                            REG_GENMASK(1, 0)

#define MTL_L4_POLICY_MASK			REG_GENMASK(3, 2)
#define MTL_PAT_3_UC				REG_FIELD_PREP(MTL_L4_POLICY_MASK, 3)
#define MTL_PAT_1_WT				REG_FIELD_PREP(MTL_L4_POLICY_MASK, 1)
#define MTL_PAT_0_WB				REG_FIELD_PREP(MTL_L4_POLICY_MASK, 0)
#define MTL_INDEX_COH_MODE_MASK			REG_GENMASK(1, 0)
#define MTL_3_COH_2W				REG_FIELD_PREP(MTL_INDEX_COH_MODE_MASK, 3)
#define MTL_2_COH_1W				REG_FIELD_PREP(MTL_INDEX_COH_MODE_MASK, 2)
#define MTL_0_COH_NON				REG_FIELD_PREP(MTL_INDEX_COH_MODE_MASK, 0)

#define PVC_CLOS_LEVEL_MASK			REG_GENMASK(3, 2)
#define PVC_PAT_CLOS(x)				REG_FIELD_PREP(PVC_CLOS_LEVEL_MASK, x)

#define TGL_MEM_TYPE_MASK			REG_GENMASK(1, 0)
#define TGL_PAT_WB				REG_FIELD_PREP(TGL_MEM_TYPE_MASK, 3)
#define TGL_PAT_WT				REG_FIELD_PREP(TGL_MEM_TYPE_MASK, 2)
#define TGL_PAT_WC				REG_FIELD_PREP(TGL_MEM_TYPE_MASK, 1)
#define TGL_PAT_UC				REG_FIELD_PREP(TGL_MEM_TYPE_MASK, 0)

static const u32 tgl_pat_table[] = {
	[0] = TGL_PAT_WB,
	[1] = TGL_PAT_WC,
	[2] = TGL_PAT_WT,
	[3] = TGL_PAT_UC,
	[4] = TGL_PAT_WB,
	[5] = TGL_PAT_WB,
	[6] = TGL_PAT_WB,
	[7] = TGL_PAT_WB,
};

static const u32 pvc_pat_table[] = {
	[0] = TGL_PAT_UC,
	[1] = TGL_PAT_WC,
	[2] = TGL_PAT_WT,
	[3] = TGL_PAT_WB,
	[4] = PVC_PAT_CLOS(1) | TGL_PAT_WT,
	[5] = PVC_PAT_CLOS(1) | TGL_PAT_WB,
	[6] = PVC_PAT_CLOS(2) | TGL_PAT_WT,
	[7] = PVC_PAT_CLOS(2) | TGL_PAT_WB,
};

static const u32 mtl_pat_table[] = {
	[0] = MTL_PAT_0_WB,
	[1] = MTL_PAT_1_WT,
	[2] = MTL_PAT_3_UC,
	[3] = MTL_PAT_0_WB | MTL_2_COH_1W,
	[4] = MTL_PAT_0_WB | MTL_3_COH_2W,
};

/*
 * The Xe2 table is getting large/complicated so it's easier to review if
 * provided in a form that exactly matches the bspec's formatting.  The meaning
 * of the fields here are:
 *   - no_promote:  0=promotable, 1=no promote
 *   - comp_en:     0=disable, 1=enable
 *   - l3clos:      L3 class of service (0-3)
 *   - l3_policy:   0=WB, 1=XD ("WB - Transient Display"), 3=UC
 *   - l4_policy:   0=WB, 1=WT, 3=UC
 *   - coh_mode:    0=no snoop, 2=1-way coherent, 3=2-way coherent
 *
 * Reserved entries should be programmed with the maximum caching, minimum
 * coherency (which matches an all-0's encoding), so we can just omit them
 * in the table.
 */
#define XE2_PAT(no_promote, comp_en, l3clos, l3_policy, l4_policy, coh_mode) \
	(no_promote ? XE2_NO_PROMOTE : 0) | \
	(comp_en ? XE2_COMP_EN : 0) | \
	REG_FIELD_PREP(XE2_L3_CLOS, l3clos) | \
	REG_FIELD_PREP(XE2_L3_POLICY, l3_policy) | \
	REG_FIELD_PREP(XE2_L4_POLICY, l4_policy) | \
	REG_FIELD_PREP(XE2_COH_MODE, coh_mode)

static const u32 xe2_pat_table[] = {
	[ 0] = XE2_PAT( 0, 0, 0, 0, 3, 0 ),
	[ 1] = XE2_PAT( 0, 0, 0, 0, 3, 2 ),
	[ 2] = XE2_PAT( 0, 0, 0, 0, 3, 3 ),
	[ 3] = XE2_PAT( 0, 0, 0, 3, 3, 0 ),
	[ 4] = XE2_PAT( 0, 0, 0, 3, 0, 2 ),
	[ 5] = XE2_PAT( 0, 0, 0, 3, 3, 2 ),
	[ 6] = XE2_PAT( 1, 0, 0, 1, 3, 0 ),
	[ 7] = XE2_PAT( 0, 0, 0, 3, 0, 3 ),
	[ 8] = XE2_PAT( 0, 0, 0, 3, 0, 0 ),
	[ 9] = XE2_PAT( 0, 1, 0, 0, 3, 0 ),
	[10] = XE2_PAT( 0, 1, 0, 3, 0, 0 ),
	[11] = XE2_PAT( 1, 1, 0, 1, 3, 0 ),
	[12] = XE2_PAT( 0, 1, 0, 3, 3, 0 ),
	[13] = XE2_PAT( 0, 0, 0, 0, 0, 0 ),
	[14] = XE2_PAT( 0, 1, 0, 0, 0, 0 ),
	[15] = XE2_PAT( 1, 1, 0, 1, 1, 0 ),
	/* 16..19 are reserved; leave set to all 0's */
	[20] = XE2_PAT( 0, 0, 1, 0, 3, 0 ),
	[21] = XE2_PAT( 0, 1, 1, 0, 3, 0 ),
	[22] = XE2_PAT( 0, 0, 1, 0, 3, 2 ),
	[23] = XE2_PAT( 0, 0, 1, 0, 3, 3 ),
	[24] = XE2_PAT( 0, 0, 2, 0, 3, 0 ),
	[25] = XE2_PAT( 0, 1, 2, 0, 3, 0 ),
	[26] = XE2_PAT( 0, 0, 2, 0, 3, 2 ),
	[27] = XE2_PAT( 0, 0, 2, 0, 3, 3 ),
	[28] = XE2_PAT( 0, 0, 3, 0, 3, 0 ),
	[29] = XE2_PAT( 0, 1, 3, 0, 3, 0 ),
	[30] = XE2_PAT( 0, 0, 3, 0, 3, 2 ),
	[31] = XE2_PAT( 0, 0, 3, 0, 3, 3 ),
};

/* Special PAT values programmed outside the main table */
#define XE2_PAT_ATS	XE2_PAT ( 0, 0, 0, 0, 3, 3 )

static void program_pat(struct xe_gt *gt, const u32 table[], int n_entries)
{
	for (int i = 0; i < n_entries; i++) {
		struct xe_reg reg = XE_REG(_PAT_INDEX(i));

		xe_mmio_write32(gt, reg, table[i]);
	}
}

static void program_pat_mcr(struct xe_gt *gt, const u32 table[], int n_entries)
{
	for (int i = 0; i < n_entries; i++) {
		struct xe_reg_mcr reg_mcr = XE_REG_MCR(_PAT_INDEX(i));

		xe_gt_mcr_multicast_write(gt, reg_mcr, table[i]);
	}
}

void xe_pat_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (xe_gt_is_media_type(gt)) {
		/*
		 * SAMedia register offsets are adjusted by the write methods
		 * and they target registers that are not MCR, while for normal
		 * GT they are MCR
		 */

		if (GRAPHICS_VER(xe) >= 20) {
			program_pat(gt, xe2_pat_table, ARRAY_SIZE(xe2_pat_table));
			xe_mmio_write32(gt, XE_REG(_PAT_ATS), XE2_PAT_ATS);
		} else if (xe->info.platform == XE_METEORLAKE) {
			program_pat(gt, mtl_pat_table, ARRAY_SIZE(mtl_pat_table));
		} else {
			drm_err(&xe->drm, "Missing PAT table for platform with media version %d.%2d!\n",
				MEDIA_VER(xe), MEDIA_VERx100(xe) % 100);
		}

		return;
	}

	if (GRAPHICS_VER(xe) >= 20) {
		program_pat_mcr(gt, xe2_pat_table, ARRAY_SIZE(xe2_pat_table));
		xe_gt_mcr_multicast_write(gt, XE_REG_MCR(_PAT_ATS), XE2_PAT_ATS);
	} else if (xe->info.platform == XE_METEORLAKE) {
		program_pat_mcr(gt, mtl_pat_table, ARRAY_SIZE(mtl_pat_table));
	} else if (xe->info.platform == XE_PVC || xe->info.platform == XE_DG2) {
		program_pat_mcr(gt, pvc_pat_table, ARRAY_SIZE(pvc_pat_table));
	} else if (GRAPHICS_VERx100(xe) <= 1210) {
		program_pat(gt, tgl_pat_table, ARRAY_SIZE(tgl_pat_table));
	} else {
		/*
		 * Going forward we expect to need new PAT settings for most
		 * new platforms; failure to provide a new table can easily
		 * lead to subtle, hard-to-debug problems.  If none of the
		 * conditions above match the platform we're running on we'll
		 * raise an error rather than trying to silently inherit the
		 * most recent platform's behavior.
		 */
		drm_err(&xe->drm, "Missing PAT table for platform with graphics version %d.%2d!\n",
			GRAPHICS_VER(xe), GRAPHICS_VERx100(xe) % 100);
	}
}

void xe_pat_dump(struct xe_gt *gt, struct drm_printer *p)
{
	u32 pat;
	int i, err;

	if (GRAPHICS_VERx100(gt_to_xe(gt)) < 2000)
		/* Not implemented yet */
		return;

	xe_device_mem_access_get(gt_to_xe(gt));
	err = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (err)
		goto err_fw;

	drm_printf(p, "PAT table:\n");

	for (i = 0; i < 32; i++) {
		if (xe_gt_is_media_type(gt))
			pat = xe_mmio_read32(gt, XE_REG(_PAT_INDEX(i)));
		else
			pat = xe_gt_mcr_unicast_read_any(gt, XE_REG_MCR(_PAT_INDEX(i)));

		drm_printf(p, "PAT[%2d] = [ %u, %u, %u, %u, %u, %u ]  (%#8x)\n", i,
			   !!(pat & XE2_NO_PROMOTE),
			   !!(pat & XE2_COMP_EN),
			   REG_FIELD_GET(XE2_L3_CLOS, pat),
			   REG_FIELD_GET(XE2_L3_POLICY, pat),
			   REG_FIELD_GET(XE2_L4_POLICY, pat),
			   REG_FIELD_GET(XE2_COH_MODE, pat),
			   pat);
	}

	/*
	 * Also print PTA_MODE, which describes how the hardware accesses
	 * PPGTT entries.
	 */
	if (xe_gt_is_media_type(gt))
		pat = xe_mmio_read32(gt, XE_REG(_PAT_PTA));
	else
		pat = xe_gt_mcr_unicast_read_any(gt, XE_REG_MCR(_PAT_PTA));

	drm_printf(p, "Page Table Access:\n");
	drm_printf(p, "PTA_MODE= [ %u, %u, %u, %u, %u, %u ]  (%#8x)\n",
		   !!(pat & XE2_NO_PROMOTE),
		   !!(pat & XE2_COMP_EN),
		   REG_FIELD_GET(XE2_L3_CLOS, pat),
		   REG_FIELD_GET(XE2_L3_POLICY, pat),
		   REG_FIELD_GET(XE2_L4_POLICY, pat),
		   REG_FIELD_GET(XE2_COH_MODE, pat),
		   pat);

	err = xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
err_fw:
	XE_WARN_ON(err);
	xe_device_mem_access_put(gt_to_xe(gt));
}
