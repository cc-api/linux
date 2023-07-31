// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 *
 * NOT_UPSTREAM: for internal use only
 */

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_module.h"
#include "xe_psmi.h"

/*
 * PSMI capture support
 *
 * Requirement for PSMI capture is to have a physically contiguous buffer.
 * The PSMI tool owns doing all necessary configuration (MMIO register
 * writes are done from user-space). However, KMD needs to provide the PSMI
 * tool with the required physical address of the base of PSMI buffer.
 *
 * VRAM backed PSMI buffer:
 * Buffer is allocated as GEM object and with XE_BO_CREATE_PINNED_BIT flag
 * which creates a contiguous allocation. The physical address is returned
 * from psmi_debugfs_capture_addr_show(). PSMI tool can mmap the buffer via
 * the PCIBAR through sysfs.
 *
 * SYSTEM memory backed PSMI buffer:
 * KMD interface here does not support allocating from SYSTEM memory region.
 * Best practice has been for the PSMI tool to allocate memory themselves
 * using hugetlbfs. In order to get the physical address, user-space can
 * query /proc/[pid]/pagemap.
 * As an alternative, CMA debugfs could also be used to allocate reserved
 * CMA memory.
 */

static int psmi_resize_object(struct xe_device *, size_t);

/*
 * Returns an address for the capture tool to use to find start of capture
 * buffer. Capture tool requires the capability to have a buffer allocated
 * per each tile (VRAM region), thus we return an address for each region.
 */
static int psmi_debugfs_capture_addr_show(struct seq_file *m, void *data)
{
	struct xe_device *xe = m->private;
	unsigned long id, region_mask;
	struct xe_bo *bo;
	u64 val;

	region_mask = xe->psmi.region_mask;
	for_each_set_bit(id, &region_mask,
			 ARRAY_SIZE(xe->psmi.capture_obj)) {
		if (id) {
			/* VRAM region */
			bo = xe->psmi.capture_obj[id];
			if (!bo)
				continue;
			/* pinned, so don't need bo_lock */
			val = __xe_bo_addr(bo, 0, PAGE_SIZE);
		} else {
			/* reserved for future SMEM support */
			val = 0;
		}
		seq_printf(m, "%ld: 0x%llx\n", id, val);
	}

	return 0;
}

/*
 * Return capture buffer size, using the size from first allocated object
 * that is found. This works because all objects must be of the same size.
 */
static int psmi_debugfs_capture_size_get(void *data, u64 *val)
{
	unsigned long id, region_mask;
	struct xe_device *xe = data;
	struct xe_bo *bo;

	region_mask = xe->psmi.region_mask;
	for_each_set_bit(id, &region_mask,
			 ARRAY_SIZE(xe->psmi.capture_obj)) {
		if (id) {
			bo = xe->psmi.capture_obj[id];
			if (bo) {
				*val = bo->size;
				return 0;
			}
		}
	}

	/* no capture objects are allocated */
	*val = 0;
	return 0;
}

/*
 * Set size of PSMI capture buffer. This triggers the allocation of
 * capture buffer in each memory region as specified with prior write
 * to psmi_capture_region_mask.
 */
static int psmi_debugfs_capture_size_set(void *data, u64 val)
{
	struct xe_device *xe = data;

	if (!enable_psmi)
		return -ENODEV;

	/* user must have specified at least one region */
	if (!xe->psmi.region_mask)
		return -EINVAL;

	return psmi_resize_object(xe, val);
}

static int psmi_debugfs_capture_region_mask_get(void *data, u64 *val)
{
	struct xe_device *xe = data;

	*val = xe->psmi.region_mask;
	return 0;
}

/*
 * Select VRAM regions for multi-tile devices, only allowed when buffer is
 * not currently allocated.
 */
static int psmi_debugfs_capture_region_mask_set(void *data, u64 region_mask)
{
	struct xe_device *xe = data;
	u64 size = 0;

	if (!enable_psmi)
		return -ENODEV;

	/* SMEM is not supported (see comments at top of file) */
	if (region_mask & 0x1)
		return -EOPNOTSUPP;

	/* input bitmask should contain only valid TTM regions */
	if (!region_mask || region_mask & ~xe->info.mem_region_mask)
		return -EINVAL;

	/* only allow setting mask if buffer is not yet allocated */
	psmi_debugfs_capture_size_get(xe, &size);
	if (size)
		return -EBUSY;

	xe->psmi.region_mask = region_mask;
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(psmi_debugfs_capture_addr);

DEFINE_DEBUGFS_ATTRIBUTE(psmi_debugfs_capture_region_mask_fops,
			 psmi_debugfs_capture_region_mask_get,
			 psmi_debugfs_capture_region_mask_set,
			 "0x%llx\n");

DEFINE_DEBUGFS_ATTRIBUTE(psmi_debugfs_capture_size_fops,
			 psmi_debugfs_capture_size_get,
			 psmi_debugfs_capture_size_set,
			 "%lld\n");

void xe_psmi_debugfs_create(struct xe_device *xe, struct dentry *fs_root)
{
	debugfs_create_file("psmi_capture_addr",
			    0400, fs_root, xe,
			    &psmi_debugfs_capture_addr_fops);

	debugfs_create_file("psmi_capture_region_mask",
			    0600, fs_root, xe,
			    &psmi_debugfs_capture_region_mask_fops);

	debugfs_create_file("psmi_capture_size",
			    0600, fs_root, xe,
			    &psmi_debugfs_capture_size_fops);
}

/*
 * Allocate GEM object for the PSMI capture buffer (in VRAM).
 * @bo_size: size in bytes
 */
static struct xe_bo *
psmi_alloc_object(struct xe_device *xe, unsigned int id, size_t bo_size)
{
	struct xe_bo *bo = NULL;
	struct xe_tile *tile;
	int err;

	if (!id || !bo_size)
		return NULL;
	tile = &xe->tiles[id - 1];

	/* VRAM: Allocate GEM object for the capture buffer */
	bo = xe_bo_create_locked(xe, tile, NULL, bo_size,
				 ttm_bo_type_kernel,
				 XE_BO_CREATE_VRAM_IF_DGFX(tile) |
				 XE_BO_CREATE_PINNED_BIT |
				 XE_BO_NEEDS_CPU_ACCESS);

	if (!IS_ERR(bo)) {
		/* Buffer written by HW, ensure stays resident */
		err = xe_bo_pin(bo);
		if (err)
			bo = ERR_PTR(err);
		xe_bo_unlock(bo);
	}

	return bo;
}

static void psmi_free_object(struct xe_bo *bo)
{
	xe_bo_lock(bo, NULL);
	xe_bo_unpin(bo);
	xe_bo_unlock(bo);
	xe_bo_put(bo);
}

/*
 * Free PSMI capture buffer objects.
 */
void xe_psmi_cleanup(struct xe_device *xe)
{
	unsigned long id, region_mask;
	struct xe_bo *bo;

	/*
	 * For total guarantee that we free all objects, iterate over known
	 * regions instead of using psmi.region_mask here.
	 */
	region_mask = xe->info.mem_region_mask;
	for_each_set_bit(id, &region_mask,
			 ARRAY_SIZE(xe->psmi.capture_obj)) {
		if (id) {
			bo = xe->psmi.capture_obj[id];
			if (bo) {
				psmi_free_object(bo);
				xe->psmi.capture_obj[id] = NULL;
			}
		}
	}
}

/*
 * Allocate PSMI capture buffer objects (via debugfs set function),
 * based on which regions the user has selected in region_mask.
 * @size: size in bytes (should be power of 2)
 *
 * Always release/free the current buffer objects before attempting to
 * allocate new ones.  Size == 0 will free all current buffers.
 *
 * Note, we don't write any registers as the capture tool is already
 * configuring all PSMI registers itself via mmio space.
 */
static int psmi_resize_object(struct xe_device *xe, size_t size)
{
	unsigned long id, region_mask = xe->psmi.region_mask;
	struct xe_bo *bo = NULL;
	int err = 0;

	/*
	 * Buddy allocator anyway will roundup to next power of 2,
	 * so rather than waste unused pages, require user to ask for
	 * power of 2 sized PSMI buffers.
	 */
	if (size && !is_power_of_2(size))
		return -EINVAL;

	/* if resizing, free currently allocated buffers first */
	xe_psmi_cleanup(xe);

	/* can set size to 0, in which case, now done */
	if (!size)
		return 0;

	for_each_set_bit(id, &region_mask,
			 ARRAY_SIZE(xe->psmi.capture_obj)) {
		if (id) {
			/* VRAM: allocate with BO */
			bo = psmi_alloc_object(xe, id, size);
			if (IS_ERR(bo)) {
				err = PTR_ERR(bo);
				break;
			}
			xe->psmi.capture_obj[id] = bo;
		}

		drm_info(&xe->drm,
			 "PSMI capture size requested: %zu bytes, allocated: %lu:%zu\n",
			 size, id, bo ? bo->size : 0);
	}

	/* on error, reverse what was allocated */
	if (err)
		xe_psmi_cleanup(xe);
	return err;
}
