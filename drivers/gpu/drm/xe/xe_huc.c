// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_huc.h"

#include "abi/gsc_binary_headers.h"
#include "regs/xe_guc_regs.h"
#include "xe_assert.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_guc.h"
#include "xe_mmio.h"
#include "xe_uc_fw.h"

static struct xe_gt *
huc_to_gt(struct xe_huc *huc)
{
	return container_of(huc, struct xe_gt, uc.huc);
}

static struct xe_device *
huc_to_xe(struct xe_huc *huc)
{
	return gt_to_xe(huc_to_gt(huc));
}

static struct xe_guc *
huc_to_guc(struct xe_huc *huc)
{
	return &container_of(huc, struct xe_uc, huc)->guc;
}

#define HUC_CSS_MODULE_TYPE 0x6
static bool css_valid(const void *data, size_t size)
{
	const struct uc_css_header *css = data;

	if (unlikely(size < sizeof(struct uc_css_header)))
		return false;

	if (css->module_type != HUC_CSS_MODULE_TYPE)
		return false;

	if (css->module_vendor != PCI_VENDOR_ID_INTEL)
		return false;

	return true;
}

static inline u32 entry_offset(const struct gsc_cpd_entry *entry)
{
	return entry->offset & GSC_CPD_ENTRY_OFFSET_MASK;
}

int xe_huc_parse_gsc_header(struct xe_uc_fw *huc_fw, const void *data, size_t size)
{
	struct xe_huc *huc = container_of(huc_fw, struct xe_huc, fw);
	struct xe_gt *gt = huc_to_gt(huc);
	const struct gsc_cpd_header_v2 *header = data;
	const struct gsc_cpd_entry *entry;
	size_t min_size = sizeof(*header);
	int i;

	if (!huc_fw->has_gsc_headers) {
		xe_gt_err(gt, "Invalid FW type for GSC header parsing!\n");
		return -EINVAL;
	}

	if (size < sizeof(*header)) {
		xe_gt_err(gt, "FW too small! %zu < %zu\n", size, min_size);
		return -ENODATA;
	}

	/*
	 * The GSC-enabled HuC binary starts with a directory header, followed
	 * by a series of entries. Each entry is identified by a name and
	 * points to a specific section of the binary containing the relevant
	 * data. The entries we're interested in are:
	 * - "HUCP.man": points to the GSC manifest header for the HuC, which
	 *               contains the version info.
	 * - "huc_fw": points to the legacy-style binary that can be used for
	 *             load via the DMA. This entry only contains a valid CSS
	 *             on binaries for platforms that support 2-step HuC load
	 *             via dma and auth via GSC (like MTL).
	 *
	 * --------------------------------------------------
	 * [  gsc_cpd_header_v2                             ]
	 * --------------------------------------------------
	 * [  gsc_cpd_entry[]                               ]
	 * [      entry1                                    ]
	 * [      ...                                       ]
	 * [      entryX                                    ]
	 * [          "HUCP.man"                            ]
	 * [           ...                                  ]
	 * [           offset  >----------------------------]------o
	 * [      ...                                       ]      |
	 * [      entryY                                    ]      |
	 * [          "huc_fw"                              ]      |
	 * [           ...                                  ]      |
	 * [           offset  >----------------------------]----------o
	 * --------------------------------------------------      |   |
	 *                                                         |   |
	 * --------------------------------------------------      |   |
	 * [ gsc_manifest_header                            ]<-----o   |
	 * [  ...                                           ]          |
	 * [  gsc_version fw_version                        ]          |
	 * [  ...                                           ]          |
	 * --------------------------------------------------          |
	 *                                                             |
	 * --------------------------------------------------          |
	 * [ data[]                                         ]<---------o
	 * [  ...                                           ]
	 * [  ...                                           ]
	 * --------------------------------------------------
	 */

	if (header->header_marker != GSC_CPD_HEADER_MARKER) {
		xe_gt_err(gt, "invalid marker for CPD header: 0x%08x!\n",
			  header->header_marker);
		return -EINVAL;
	}

	/* we only have binaries with header v2 and entry v1 for now */
	if (header->header_version != 2 || header->entry_version != 1) {
		xe_gt_err(gt, "invalid CPD header/entry version %u:%u!\n",
			  header->header_version, header->entry_version);
		return -EINVAL;
	}

	if (header->header_length < sizeof(struct gsc_cpd_header_v2)) {
		xe_gt_err(gt, "invalid CPD header length %u!\n", header->header_length);
		return -EINVAL;
	}

	min_size = header->header_length + sizeof(*entry) * header->num_of_entries;
	if (size < min_size) {
		xe_gt_err(gt, "FW too small! %zu < %zu\n", size, min_size);
		return -ENODATA;
	}

	entry = data + header->header_length;

	for (i = 0; i < header->num_of_entries; i++, entry++) {
		if (strcmp(entry->name, "HUCP.man") == 0) {
			const struct gsc_manifest_header *manifest = data + entry_offset(entry);

			huc_fw->major_ver_found = manifest->fw_version.major;
			huc_fw->minor_ver_found = manifest->fw_version.minor;
		}

		if (strcmp(entry->name, "huc_fw") == 0) {
			u32 offset = entry_offset(entry);

			if (offset < size && css_valid(data + offset, size - offset))
				huc_fw->dma_start_offset = offset;
		}
	}

	return 0;
}

int xe_huc_init(struct xe_huc *huc)
{
	struct xe_gt *gt = huc_to_gt(huc);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = gt_to_xe(gt);
	int ret;

	huc->fw.type = XE_UC_FW_TYPE_HUC;

	/* On platforms with a media GT the HuC is only available there */
	if (tile->media_gt && (gt != tile->media_gt)) {
		xe_uc_fw_change_status(&huc->fw, XE_UC_FIRMWARE_NOT_SUPPORTED);
		return 0;
	}

	ret = xe_uc_fw_init(&huc->fw);
	if (ret)
		goto out;

	if (!xe_uc_fw_is_enabled(&huc->fw))
		return 0;

	xe_uc_fw_change_status(&huc->fw, XE_UC_FIRMWARE_LOADABLE);

	return 0;

out:
	drm_err(&xe->drm, "HuC init failed with %d", ret);
	return ret;
}

int xe_huc_upload(struct xe_huc *huc)
{
	if (!xe_uc_fw_is_loadable(&huc->fw))
		return 0;
	return xe_uc_fw_upload(&huc->fw, 0, HUC_UKERNEL);
}

int xe_huc_auth(struct xe_huc *huc)
{
	struct xe_device *xe = huc_to_xe(huc);
	struct xe_gt *gt = huc_to_gt(huc);
	struct xe_guc *guc = huc_to_guc(huc);
	int ret;

	if (!xe_uc_fw_is_loadable(&huc->fw))
		return 0;

	xe_assert(xe, !xe_uc_fw_is_running(&huc->fw));

	if (!xe_uc_fw_is_loaded(&huc->fw))
		return -ENOEXEC;

	ret = xe_guc_auth_huc(guc, xe_bo_ggtt_addr(huc->fw.bo) +
			      xe_uc_fw_rsa_offset(&huc->fw));
	if (ret) {
		drm_err(&xe->drm, "HuC: GuC did not ack Auth request %d\n",
			ret);
		goto fail;
	}

	ret = xe_mmio_wait32(gt, HUC_KERNEL_LOAD_INFO, HUC_LOAD_SUCCESSFUL,
			     HUC_LOAD_SUCCESSFUL, 100000, NULL, false);
	if (ret) {
		drm_err(&xe->drm, "HuC: Firmware not verified %d\n", ret);
		goto fail;
	}

	xe_uc_fw_change_status(&huc->fw, XE_UC_FIRMWARE_RUNNING);
	drm_dbg(&xe->drm, "HuC authenticated\n");

	return 0;

fail:
	drm_err(&xe->drm, "HuC authentication failed %d\n", ret);
	xe_uc_fw_change_status(&huc->fw, XE_UC_FIRMWARE_LOAD_FAIL);

	return ret;
}

void xe_huc_sanitize(struct xe_huc *huc)
{
	if (!xe_uc_fw_is_loadable(&huc->fw))
		return;
	xe_uc_fw_change_status(&huc->fw, XE_UC_FIRMWARE_LOADABLE);
}

void xe_huc_print_info(struct xe_huc *huc, struct drm_printer *p)
{
	struct xe_gt *gt = huc_to_gt(huc);
	int err;

	xe_uc_fw_print(&huc->fw, p);

	if (!xe_uc_fw_is_enabled(&huc->fw))
		return;

	err = xe_force_wake_get(gt_to_fw(gt), XE_FW_GT);
	if (err)
		return;

	drm_printf(p, "\nHuC status: 0x%08x\n",
		   xe_mmio_read32(gt, HUC_KERNEL_LOAD_INFO));

	xe_force_wake_put(gt_to_fw(gt), XE_FW_GT);
}
