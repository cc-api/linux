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

#define _ERR_STAT_GT_FATAL_VCTR_0       0x100260
#define _ERR_STAT_GT_FATAL_VCTR_1       0x100264
#define ERR_STAT_GT_FATAL_VCTR_REG(x)   XE_REG(_PICK_EVEN((x), \
						_ERR_STAT_GT_FATAL_VCTR_0, \
						_ERR_STAT_GT_FATAL_VCTR_1))

#define _ERR_STAT_GT_COR_VCTR_0         0x1002a0
#define _ERR_STAT_GT_COR_VCTR_1         0x1002a4
#define ERR_STAT_GT_COR_VCTR_REG(x)     XE_REG(_PICK_EVEN((x), \
						_ERR_STAT_GT_COR_VCTR_0, \
						_ERR_STAT_GT_COR_VCTR_1))

#define ERR_STAT_GT_VCTR_REG(hw_err, x) (hw_err == HARDWARE_ERROR_CORRECTABLE ? \
						ERR_STAT_GT_COR_VCTR_REG(x) : \
						ERR_STAT_GT_FATAL_VCTR_REG(x))
#endif
