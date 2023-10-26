/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */
#ifndef XE_TILE_ERROR_REGS_H_
#define XE_TILE_ERROR_REGS_H_

#define _DEV_ERR_STAT_NONFATAL                         0x100178
#define _DEV_ERR_STAT_CORRECTABLE                      0x10017c
#define DEV_ERR_STAT_REG(x)                            XE_REG(_PICK_EVEN((x), \
								_DEV_ERR_STAT_CORRECTABLE, \
								_DEV_ERR_STAT_NONFATAL))
#endif
