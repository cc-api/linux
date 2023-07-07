/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PAT_H_
#define _XE_PAT_H_

struct xe_gt;
struct drm_printer;

void xe_pat_init(struct xe_gt *gt);
void xe_pat_dump(struct xe_gt *gt, struct drm_printer *p);

#endif
