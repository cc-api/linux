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
#define _SOC_GCOERRSTS		                       0x200
#define _SOC_GNFERRSTS		                       0x210
#define _SOC_GFAERRSTS		                       0x220
#define SOC_GLOBAL_ERR_STAT_SLAVE_REG(base, x)		XE_REG(_PICK_EVEN((x), \
								(base) + _SOC_GCOERRSTS, \
								(base) + _SOC_GNFERRSTS))
#define   SOC_IEH1_LOCAL_ERR_STATUS                    0

#define SOC_GLOBAL_ERR_STAT_MASTER_REG(base, x)		XE_REG(_PICK_EVEN((x), \
								(base) + _SOC_GCOERRSTS, \
								(base) + _SOC_GNFERRSTS))
#define   SOC_IEH0_LOCAL_ERR_STATUS                    0
#define   SOC_IEH1_GLOBAL_ERR_STATUS                   1

#define _SOC_GSYSEVTCTL		                       0x264
#define SOC_GSYSEVTCTL_REG(base, slave_base, x)		XE_REG(_PICK_EVEN((x), \
								(base) + _SOC_GSYSEVTCTL, \
								slave_base + _SOC_GSYSEVTCTL))

#define _SOC_LERRCORSTS		                       0x294
#define _SOC_LERRUNCSTS		                       0x280
#define SOC_LOCAL_ERR_STAT_SLAVE_REG(base, x)		XE_REG((x) > HARDWARE_ERROR_CORRECTABLE ? \
								(base) + _SOC_LERRUNCSTS : \
								(base) + _SOC_LERRCORSTS)
#define SOC_LOCAL_ERR_STAT_MASTER_REG(base, x)		XE_REG((x) > HARDWARE_ERROR_CORRECTABLE ? \
								(base) + _SOC_LERRUNCSTS : \
								(base) + _SOC_LERRCORSTS)
#define   MDFI_T2T                                     4
#define   MDFI_T2C                                     6


#define _DEV_ERR_STAT_NONFATAL                         0x100178
#define _DEV_ERR_STAT_CORRECTABLE                      0x10017c
#define DEV_ERR_STAT_REG(x)                            XE_REG(_PICK_EVEN((x), \
								_DEV_ERR_STAT_CORRECTABLE, \
								_DEV_ERR_STAT_NONFATAL))
#define   XE_GT_ERROR				       0
#define   XE_GSC_ERROR				       8
#define   XE_SOC_ERROR                                 16

#define SOC_PVC_BASE	                               0x282000

#define LOCAL_FIRST_IEH_HEADER_LOG_REG		       XE_REG(0x2822b0)
#define MDFI_SEVERITY_FATAL		               0x00330000
#define MDFI_SEVERITY_NONFATAL		               0x00310000
#define MDFI_SEVERITY(x)			       ((x) == HARDWARE_ERROR_FATAL ? \
							       MDFI_SEVERITY_FATAL : \
							       MDFI_SEVERITY_NONFATAL)
#define SOC_PVC_SLAVE_BASE                             0x283000

#define PVC_GSC_HECI1_BASE                             0x284000

#endif
