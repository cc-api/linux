/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */
#ifndef XE_TILE_ERROR_REGS_H_
#define XE_TILE_ERROR_REGS_H_


#define _GSC_HEC_UNCOR_ERR_STATUS                      0x118
#define _GSC_HEC_CORR_ERR_STATUS                       0x128
#define GSC_HEC_ERR_STAT_REG(base, x)                  XE_REG(_PICK_EVEN((x), \
								(base) + _GSC_HEC_CORR_ERR_STATUS, \
								(base) + _GSC_HEC_UNCOR_ERR_STATUS))

#define _DEV_ERR_STAT_NONFATAL                         0x100178
#define _DEV_ERR_STAT_CORRECTABLE                      0x10017c
#define DEV_ERR_STAT_REG(x)                            XE_REG(_PICK_EVEN((x), \
								_DEV_ERR_STAT_CORRECTABLE, \
								_DEV_ERR_STAT_NONFATAL))
#define   XE_GT_ERROR				       0
#define   XE_GSC_ERROR				       8

#define PVC_GSC_HECI1_BASE                             0x284000

#endif
