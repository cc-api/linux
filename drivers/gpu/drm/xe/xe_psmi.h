/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PSMI_H_
#define _XE_PSMI_H_

struct xe_device;
struct dentry;

void xe_psmi_cleanup(struct xe_device *xe);
void xe_psmi_debugfs_create(struct xe_device *xe, struct dentry *fs_root);
#endif
