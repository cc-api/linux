/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */
#ifndef XE_GT_ERROR_REGS_H_
#define XE_GT_ERROR_REGS_H_

#define _ERR_STAT_GT_COR                0x100160
#define _ERR_STAT_GT_NONFATAL           0x100164
#define ERR_STAT_GT_REG(x)              XE_REG(_PICK_EVEN((x), \
						_ERR_STAT_GT_COR, \
						_ERR_STAT_GT_NONFATAL))
#endif
