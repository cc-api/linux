// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/xe_drm.h>

#include "xe_hw_error.h"

#include "regs/xe_regs.h"
#include "regs/xe_gt_error_regs.h"
#include "regs/xe_tile_error_regs.h"
#include "xe_device.h"
#include "xe_mmio.h"

static const char *
hardware_error_type_to_str(const enum hardware_error hw_err)
{
	switch (hw_err) {
	case HARDWARE_ERROR_CORRECTABLE:
		return "CORRECTABLE";
	case HARDWARE_ERROR_NONFATAL:
		return "NONFATAL";
	case HARDWARE_ERROR_FATAL:
		return "FATAL";
	default:
		return "UNKNOWN";
	}
}

static const struct err_name_index_pair dg2_err_stat_fatal_reg[] = {
	[0]         = {"GT",			XE_HW_ERR_TILE_UNSPEC},
	[1 ... 3]   = {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[4 ... 7]   = {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[8]         = {"GSC",			XE_HW_ERR_TILE_UNSPEC},
	[9 ... 11]  = {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[12]        = {"SGUNIT",		XE_HW_ERR_TILE_FATAL_SGUNIT},
	[13 ... 15] = {"Undefined",             XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[16]        = {"SOC",			XE_HW_ERR_TILE_UNSPEC},
	[17 ... 31] = {"Undefined",             XE_HW_ERR_TILE_FATAL_UNKNOWN},
};

static const struct err_name_index_pair dg2_err_stat_nonfatal_reg[] = {
	[0]         = {"GT",			XE_HW_ERR_TILE_UNSPEC},
	[1 ... 3]   = {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[4 ... 7]   = {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[8]         = {"GSC",			XE_HW_ERR_TILE_UNSPEC},
	[9 ... 11]  = {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[12]        = {"SGUNIT",		XE_HW_ERR_TILE_NONFATAL_SGUNIT},
	[13 ... 15] = {"Undefined",             XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[16]        = {"SOC",			XE_HW_ERR_TILE_UNSPEC},
	[17 ... 19] = {"Undefined",             XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[20]        = {"MERT",			XE_HW_ERR_TILE_NONFATAL_MERT},
	[21 ... 31] = {"Undefined",             XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
};

static const struct err_name_index_pair dg2_err_stat_correctable_reg[] = {
	[0]         = {"GT",			XE_HW_ERR_TILE_UNSPEC},
	[1 ... 3]   = {"Undefined",		XE_HW_ERR_TILE_CORR_UNKNOWN},
	[4 ... 7]   = {"Undefined",		XE_HW_ERR_TILE_CORR_UNKNOWN},
	[8]         = {"GSC",			XE_HW_ERR_TILE_UNSPEC},
	[9 ... 11]  = {"Undefined",		XE_HW_ERR_TILE_CORR_UNKNOWN},
	[12]        = {"SGUNIT",		XE_HW_ERR_TILE_CORR_SGUNIT},
	[13 ... 15] = {"Undefined",             XE_HW_ERR_TILE_CORR_UNKNOWN},
	[16]        = {"SOC",			XE_HW_ERR_TILE_UNSPEC},
	[17 ... 31] = {"Undefined",             XE_HW_ERR_TILE_CORR_UNKNOWN},
};

static const struct err_name_index_pair pvc_err_stat_fatal_reg[] = {
	[0]         =  {"GT",			XE_HW_ERR_TILE_UNSPEC},
	[1]         =  {"SGGI Cmd Parity",	XE_HW_ERR_TILE_FATAL_SGGI},
	[2 ... 7]   =  {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[8]         =  {"GSC",			XE_HW_ERR_TILE_UNSPEC},
	[9]         =  {"SGLI Cmd Parity",	XE_HW_ERR_TILE_FATAL_SGLI},
	[10 ... 12] =  {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[13]        =  {"SGCI Cmd Parity",	XE_HW_ERR_TILE_FATAL_SGCI},
	[14 ... 15] =  {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[16]        =  {"SOC ERROR",		XE_HW_ERR_TILE_UNSPEC},
	[17 ... 19] =  {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
	[20]        =  {"MERT Cmd Parity",	XE_HW_ERR_TILE_FATAL_MERT},
	[21 ... 31] =  {"Undefined",		XE_HW_ERR_TILE_FATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_err_stat_nonfatal_reg[] = {
	[0]         =  {"GT",			XE_HW_ERR_TILE_UNSPEC},
	[1]         =  {"SGGI Data Parity",	XE_HW_ERR_TILE_NONFATAL_SGGI},
	[2 ... 7]   =  {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[8]         =  {"GSC",			XE_HW_ERR_TILE_UNSPEC},
	[9]         =  {"SGLI Data Parity",	XE_HW_ERR_TILE_NONFATAL_SGLI},
	[10 ... 12] =  {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[13]        =  {"SGCI Data Parity",	XE_HW_ERR_TILE_NONFATAL_SGCI},
	[14 ... 15] =  {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[16]        =  {"SOC",			XE_HW_ERR_TILE_UNSPEC},
	[17 ... 19] =  {"Undefined",		XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
	[20]        =  {"MERT Data Parity",	XE_HW_ERR_TILE_NONFATAL_MERT},
	[21 ... 31] =  {"Undefined",            XE_HW_ERR_TILE_NONFATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_err_stat_correctable_reg[] = {
	[0]         =  {"GT",			XE_HW_ERR_TILE_UNSPEC},
	[1 ... 7]   =  {"Undefined",		XE_HW_ERR_TILE_CORR_UNKNOWN},
	[8]         =  {"GSC",			XE_HW_ERR_TILE_UNSPEC},
	[9 ... 31]  =  {"Undefined",		XE_HW_ERR_TILE_CORR_UNKNOWN},
};

static const struct err_name_index_pair dg2_stat_gt_fatal_reg[] = {
	[0]         =  {"Undefined",		XE_HW_ERR_GT_FATAL_UNKNOWN},
	[1]         =  {"Array BIST",		XE_HW_ERR_GT_FATAL_ARR_BIST},
	[2]         =  {"Undefined",		XE_HW_ERR_GT_FATAL_UNKNOWN},
	[3]         =  {"FPU",			XE_HW_ERR_GT_FATAL_FPU},
	[4]         =  {"L3 Double",		XE_HW_ERR_GT_FATAL_L3_DOUB},
	[5]         =  {"L3 ECC Checker",	XE_HW_ERR_GT_FATAL_L3_ECC_CHK},
	[6]         =  {"GUC SRAM",		XE_HW_ERR_GT_FATAL_GUC},
	[7]         =  {"Undefined",		XE_HW_ERR_GT_FATAL_UNKNOWN},
	[8]         =  {"IDI PARITY",		XE_HW_ERR_GT_FATAL_IDI_PAR},
	[9]	    =  {"SQIDI",		XE_HW_ERR_GT_FATAL_SQIDI},
	[10 ... 11] =  {"Undefined",		XE_HW_ERR_GT_FATAL_UNKNOWN},
	[12]        =  {"SAMPLER",		XE_HW_ERR_GT_FATAL_SAMPLER},
	[13]        =  {"SLM",			XE_HW_ERR_GT_FATAL_SLM},
	[14]        =  {"EU IC",		XE_HW_ERR_GT_FATAL_EU_IC},
	[15]        =  {"EU GRF",		XE_HW_ERR_GT_FATAL_EU_GRF},
	[16 ... 31] =  {"Undefined",            XE_HW_ERR_GT_FATAL_UNKNOWN},
};

static const struct err_name_index_pair dg2_stat_gt_correctable_reg[] = {
	[0]         = {"L3 SINGLE",		XE_HW_ERR_GT_CORR_L3_SNG},
	[1]         = {"SINGLE BIT GUC SRAM",	XE_HW_ERR_GT_CORR_GUC},
	[2 ... 11]  = {"Undefined",		XE_HW_ERR_GT_CORR_UNKNOWN},
	[12]        = {"SINGLE BIT SAMPLER",	XE_HW_ERR_GT_CORR_SAMPLER},
	[13]        = {"SINGLE BIT SLM",	XE_HW_ERR_GT_CORR_SLM},
	[14]        = {"SINGLE BIT EU IC",	XE_HW_ERR_GT_CORR_EU_IC},
	[15]        = {"SINGLE BIT EU GRF",	XE_HW_ERR_GT_CORR_EU_GRF},
	[16 ... 31] = {"Undefined",             XE_HW_ERR_GT_CORR_UNKNOWN},
};

static const struct err_name_index_pair pvc_err_stat_gt_fatal_reg[] = {
	[0 ... 2]   =  {"Undefined",		XE_HW_ERR_GT_FATAL_UNKNOWN},
	[3]         =  {"FPU",			XE_HW_ERR_GT_FATAL_FPU},
	[4 ... 5]   =  {"Undefined",            XE_HW_ERR_GT_FATAL_UNKNOWN},
	[6]         =  {"GUC SRAM",		XE_HW_ERR_GT_FATAL_GUC},
	[7 ... 12]  =  {"Undefined",		XE_HW_ERR_GT_FATAL_UNKNOWN},
	[13]        =  {"SLM",			XE_HW_ERR_GT_FATAL_SLM},
	[14]        =  {"Undefined",		XE_HW_ERR_GT_FATAL_UNKNOWN},
	[15]        =  {"EU GRF",		XE_HW_ERR_GT_FATAL_EU_GRF},
	[16 ... 31] =  {"Undefined",            XE_HW_ERR_GT_FATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_err_stat_gt_correctable_reg[] = {
	[0]         = {"Undefined",             XE_HW_ERR_GT_CORR_UNKNOWN},
	[1]         = {"SINGLE BIT GUC SRAM",	XE_HW_ERR_GT_CORR_GUC},
	[2 ... 12]  = {"Undefined",		XE_HW_ERR_GT_CORR_UNKNOWN},
	[13]        = {"SINGLE BIT SLM",	XE_HW_ERR_GT_CORR_SLM},
	[14]        = {"SINGLE BIT EU IC",	XE_HW_ERR_GT_CORR_EU_IC},
	[15]        = {"SINGLE BIT EU GRF",	XE_HW_ERR_GT_CORR_EU_GRF},
	[16 ... 31] = {"Undefined",             XE_HW_ERR_GT_CORR_UNKNOWN},
};

static const struct err_name_index_pair pvc_err_vectr_gt_fatal_reg[] = {
	[0 ... 1]         = {"SUBSLICE",	XE_HW_ERR_GT_FATAL_SUBSLICE},
	[2 ... 3]         = {"L3BANK",		XE_HW_ERR_GT_FATAL_L3BANK},
	[4 ... 5]         = {"Undefined",	XE_HW_ERR_GT_FATAL_UNKNOWN},
	[6]               = {"TLB",		XE_HW_ERR_GT_FATAL_TLB},
	[7]               = {"L3 FABRIC",	XE_HW_ERR_GT_FATAL_L3_FABRIC},
};

static const struct err_name_index_pair pvc_err_vectr_gt_correctable_reg[] = {
	[0 ... 1]         = {"SUBSLICE",	XE_HW_ERR_GT_CORR_SUBSLICE},
	[2 ... 3]         = {"L3BANK",		XE_HW_ERR_GT_CORR_L3BANK},
	[4 ... 7]         = {"Undefined",       XE_HW_ERR_GT_CORR_UNKNOWN},
};

static const struct err_name_index_pair pvc_gsc_nonfatal_err_reg[] = {
	[0]  = {"MinuteIA Unexpected Shutdown",		 XE_HW_ERR_GSC_NONFATAL_MIA_SHUTDOWN},
	[1]  = {"MinuteIA Internal Error",		 XE_HW_ERR_GSC_NONFATAL_MIA_INTERNAL},
	[2]  = {"Double bit error on SRAM",		 XE_HW_ERR_GSC_NONFATAL_SRAM},
	[3]  = {"WDT 2nd Timeout",			 XE_HW_ERR_GSC_NONFATAL_WDG},
	[4]  = {"ROM has a parity error",		 XE_HW_ERR_GSC_NONFATAL_ROM_PARITY},
	[5]  = {"Ucode has a parity error",		 XE_HW_ERR_GSC_NONFATAL_UCODE_PARITY},
	[6]  = {"Errors Reported to and Detected by FW", XE_HW_ERR_TILE_UNSPEC},
	[7]  = {"Glitch is detected on voltage rail",	 XE_HW_ERR_GSC_NONFATAL_VLT_GLITCH},
	[8]  = {"Fuse Pull Error",			 XE_HW_ERR_GSC_NONFATAL_FUSE_PULL},
	[9]  = {"Fuse CRC Check Failed on Fuse Pull",	 XE_HW_ERR_GSC_NONFATAL_FUSE_CRC},
	[10] = {"Self Mbist Failed",			 XE_HW_ERR_GSC_NONFATAL_SELF_MBIST},
	[11] = {"AON RF has parity error",		 XE_HW_ERR_GSC_NONFATAL_AON_RF_PARITY},
	[12 ... 31] = {"Undefined",			 XE_HW_ERR_GSC_NONFATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_gsc_correctable_err_reg[] = {
	[0]  = {"Single bit error on SRAM",			XE_HW_ERR_GSC_CORR_SRAM},
	[1]  = {"Errors Reported to FW and Detected by FW",	XE_HW_ERR_TILE_UNSPEC},
	[2 ... 31] = {"Undefined",				XE_HW_ERR_GSC_CORR_UNKNOWN},
};

static const struct err_name_index_pair pvc_soc_mstr_glbl_err_reg_fatal[] = {
	[0]         = {"MASTER LOCAL Reported",			XE_HW_ERR_TILE_UNSPEC},
	[1]         = {"SLAVE GLOBAL Reported",			XE_HW_ERR_TILE_UNSPEC},
	[2]         = {"HBM SS0: Channel0",			XE_HW_ERR_SOC_FATAL_HBM0_CHNL0},
	[3]         = {"HBM SS0: Channel1",			XE_HW_ERR_SOC_FATAL_HBM0_CHNL1},
	[4]         = {"HBM SS0: Channel2",			XE_HW_ERR_SOC_FATAL_HBM0_CHNL2},
	[5]         = {"HBM SS0: Channel3",			XE_HW_ERR_SOC_FATAL_HBM0_CHNL3},
	[6]         = {"HBM SS0: Channel4",			XE_HW_ERR_SOC_FATAL_HBM0_CHNL4},
	[7]         = {"HBM SS0: Channel5",			XE_HW_ERR_SOC_FATAL_HBM0_CHNL5},
	[8]         = {"HBM SS0: Channel6",                     XE_HW_ERR_SOC_FATAL_HBM0_CHNL6},
	[9]         = {"HBM SS0: Channel7",                     XE_HW_ERR_SOC_FATAL_HBM0_CHNL7},
	[10]        = {"HBM SS1: Channel0",                     XE_HW_ERR_SOC_FATAL_HBM1_CHNL0},
	[11]        = {"HBM SS1: Channel1",                     XE_HW_ERR_SOC_FATAL_HBM1_CHNL1},
	[12]        = {"HBM SS1: Channel2",                     XE_HW_ERR_SOC_FATAL_HBM1_CHNL2},
	[13]        = {"HBM SS1: Channel3",                     XE_HW_ERR_SOC_FATAL_HBM1_CHNL3},
	[14]        = {"HBM SS1: Channel4",                     XE_HW_ERR_SOC_FATAL_HBM1_CHNL4},
	[15]        = {"HBM SS1: Channel5",                     XE_HW_ERR_SOC_FATAL_HBM1_CHNL5},
	[16]        = {"HBM SS1: Channel6",                     XE_HW_ERR_SOC_FATAL_HBM1_CHNL6},
	[17]        = {"HBM SS1: Channel7",                     XE_HW_ERR_SOC_FATAL_HBM1_CHNL7},
	[18]	    = {"PUNIT",					XE_HW_ERR_SOC_FATAL_PUNIT},
	[19 ... 31] = {"Undefined",				XE_HW_ERR_SOC_FATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_soc_slave_glbl_err_reg_fatal[] = {
	[0]         = {"SLAVE LOCAL Reported",			XE_HW_ERR_TILE_UNSPEC},
	[1]         = {"HBM SS2: Channel0",			XE_HW_ERR_SOC_FATAL_HBM2_CHNL0},
	[2]         = {"HBM SS2: Channel1",			XE_HW_ERR_SOC_FATAL_HBM2_CHNL1},
	[3]         = {"HBM SS2: Channel2",			XE_HW_ERR_SOC_FATAL_HBM2_CHNL2},
	[4]         = {"HBM SS2: Channel3",			XE_HW_ERR_SOC_FATAL_HBM2_CHNL3},
	[5]         = {"HBM SS2: Channel4",			XE_HW_ERR_SOC_FATAL_HBM2_CHNL4},
	[6]         = {"HBM SS2: Channel5",			XE_HW_ERR_SOC_FATAL_HBM2_CHNL5},
	[7]         = {"HBM SS2: Channel6",                     XE_HW_ERR_SOC_FATAL_HBM2_CHNL6},
	[8]         = {"HBM SS2: Channel7",                     XE_HW_ERR_SOC_FATAL_HBM2_CHNL7},
	[9]         = {"HBM SS3: Channel0",                     XE_HW_ERR_SOC_FATAL_HBM3_CHNL0},
	[10]        = {"HBM SS3: Channel1",                     XE_HW_ERR_SOC_FATAL_HBM3_CHNL1},
	[11]        = {"HBM SS3: Channel2",                     XE_HW_ERR_SOC_FATAL_HBM3_CHNL2},
	[12]        = {"HBM SS3: Channel3",                     XE_HW_ERR_SOC_FATAL_HBM3_CHNL3},
	[13]        = {"HBM SS3: Channel4",                     XE_HW_ERR_SOC_FATAL_HBM3_CHNL4},
	[14]        = {"HBM SS3: Channel5",                     XE_HW_ERR_SOC_FATAL_HBM3_CHNL5},
	[15]        = {"HBM SS3: Channel6",                     XE_HW_ERR_SOC_FATAL_HBM3_CHNL6},
	[16]        = {"HBM SS3: Channel7",                     XE_HW_ERR_SOC_FATAL_HBM3_CHNL7},
	[18]	    = {"ANR MDFI",				XE_HW_ERR_SOC_FATAL_ANR_MDFI},
	[17]        = {"Undefined",                             XE_HW_ERR_SOC_FATAL_UNKNOWN},
	[19 ... 31] = {"Undefined",				XE_HW_ERR_SOC_FATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_soc_slave_lcl_err_reg_fatal[] = {
	[0]         = {"Local IEH Internal: Malformed PCIe AER",     XE_HW_ERR_SOC_FATAL_PCIE_AER},
	[1]         = {"Local IEH Internal: Malformed PCIe ERR",     XE_HW_ERR_SOC_FATAL_PCIE_ERR},
	[2]         = {"Local IEH Internal: UR CONDITIONS IN IEH",   XE_HW_ERR_SOC_FATAL_UR_COND},
	[3]         = {"Local IEH Internal: FROM SERR SOURCES",      XE_HW_ERR_SOC_FATAL_SERR_SRCS},
	[4 ... 31]  = {"Undefined",				     XE_HW_ERR_SOC_FATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_soc_mstr_lcl_err_reg_fatal[] = {
	[0 ... 3]   = {"Undefined",				XE_HW_ERR_SOC_FATAL_UNKNOWN},
	[4]         = {"Base Die MDFI T2T",			XE_HW_ERR_SOC_FATAL_MDFI_T2T},
	[5]         = {"Undefined",				XE_HW_ERR_SOC_FATAL_UNKNOWN},
	[6]         = {"Base Die MDFI T2C",			XE_HW_ERR_SOC_FATAL_MDFI_T2C},
	[7]         = {"Undefined",				XE_HW_ERR_SOC_FATAL_UNKNOWN},
	[8]         = {"Invalid CSC PSF Command Parity",	XE_HW_ERR_SOC_FATAL_CSC_PSF_CMD},
	[9]         = {"Invalid CSC PSF Unexpected Completion",	XE_HW_ERR_SOC_FATAL_CSC_PSF_CMP},
	[10]        = {"Invalid CSC PSF Unsupported Request",	XE_HW_ERR_SOC_FATAL_CSC_PSF_REQ},
	[11]        = {"Invalid PCIe PSF Command Parity",	XE_HW_ERR_SOC_FATAL_PCIE_PSF_CMD},
	[12]        = {"PCIe PSF Unexpected Completion",	XE_HW_ERR_SOC_FATAL_PCIE_PSF_CMP},
	[13]        = {"PCIe PSF Unsupported Request",		XE_HW_ERR_SOC_FATAL_PCIE_PSF_REQ},
	[14 ... 31] = {"Undefined",				XE_HW_ERR_SOC_FATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_soc_mstr_glbl_err_reg_nonfatal[] = {
	[0]         = {"MASTER LOCAL Reported",			XE_HW_ERR_TILE_UNSPEC},
	[1]         = {"SLAVE GLOBAL Reported",			XE_HW_ERR_TILE_UNSPEC},
	[2]         = {"HBM SS0: Channel0",			XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL0},
	[3]         = {"HBM SS0: Channel1",			XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL1},
	[4]         = {"HBM SS0: Channel2",			XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL2},
	[5]         = {"HBM SS0: Channel3",			XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL3},
	[6]         = {"HBM SS0: Channel4",			XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL4},
	[7]         = {"HBM SS0: Channel5",			XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL5},
	[8]         = {"HBM SS0: Channel6",                     XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL6},
	[9]         = {"HBM SS0: Channel7",                     XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL7},
	[10]        = {"HBM SS1: Channel0",                     XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL0},
	[11]        = {"HBM SS1: Channel1",                     XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL1},
	[12]        = {"HBM SS1: Channel2",                     XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL2},
	[13]        = {"HBM SS1: Channel3",                     XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL3},
	[14]        = {"HBM SS1: Channel4",                     XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL4},
	[15]        = {"HBM SS1: Channel5",                     XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL5},
	[16]        = {"HBM SS1: Channel6",                     XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL6},
	[17]        = {"HBM SS1: Channel7",                     XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL7},
	[18 ... 31] = {"Undefined",				XE_HW_ERR_SOC_NONFATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_soc_slave_glbl_err_reg_nonfatal[] = {
	[0]         = {"SLAVE LOCAL Reported",			XE_HW_ERR_TILE_UNSPEC},
	[1]         = {"HBM SS2: Channel0",			XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL0},
	[2]         = {"HBM SS2: Channel1",			XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL1},
	[3]         = {"HBM SS2: Channel2",			XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL2},
	[4]         = {"HBM SS2: Channel3",			XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL3},
	[5]         = {"HBM SS2: Channel4",			XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL4},
	[6]         = {"HBM SS2: Channel5",			XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL5},
	[7]         = {"HBM SS2: Channel6",                     XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL6},
	[8]         = {"HBM SS2: Channel7",                     XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL7},
	[9]         = {"HBM SS3: Channel0",                     XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL0},
	[10]        = {"HBM SS3: Channel1",                     XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL1},
	[11]        = {"HBM SS3: Channel2",                     XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL2},
	[12]        = {"HBM SS3: Channel3",                     XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL3},
	[13]        = {"HBM SS3: Channel4",                     XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL4},
	[14]        = {"HBM SS3: Channel5",                     XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL5},
	[15]        = {"HBM SS3: Channel6",                     XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL6},
	[16]        = {"HBM SS3: Channel7",                     XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL7},
	[18]	    = {"ANR MDFI",				XE_HW_ERR_SOC_NONFATAL_ANR_MDFI},
	[17]        = {"Undefined",                             XE_HW_ERR_SOC_NONFATAL_UNKNOWN},
	[19 ... 31] = {"Undefined",				XE_HW_ERR_SOC_NONFATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_soc_slave_lcl_err_reg_nonfatal[] = {
	[0 ... 31]  = {"Undefined",			XE_HW_ERR_SOC_NONFATAL_UNKNOWN},
};

static const struct err_name_index_pair pvc_soc_mstr_lcl_err_reg_nonfatal[] = {
	[0 ... 3]   = {"Undefined",				XE_HW_ERR_SOC_NONFATAL_UNKNOWN},
	[4]         = {"Base Die MDFI T2T",			XE_HW_ERR_SOC_NONFATAL_MDFI_T2T},
	[5]         = {"Undefined",				XE_HW_ERR_SOC_NONFATAL_UNKNOWN},
	[6]         = {"Base Die MDFI T2C",			XE_HW_ERR_SOC_NONFATAL_MDFI_T2C},
	[7]         = {"Undefined",				XE_HW_ERR_SOC_NONFATAL_UNKNOWN},
	[8]         = {"Invalid CSC PSF Command Parity",	XE_HW_ERR_SOC_NONFATAL_CSC_PSF_CMD},
	[9]         = {"Invalid CSC PSF Unexpected Completion",	XE_HW_ERR_SOC_NONFATAL_CSC_PSF_CMP},
	[10]        = {"Invalid CSC PSF Unsupported Request",	XE_HW_ERR_SOC_NONFATAL_CSC_PSF_REQ},
	[11 ... 31] = {"Undefined",				XE_HW_ERR_SOC_NONFATAL_UNKNOWN},
};

void xe_assign_hw_err_regs(struct xe_device *xe)
{
	const struct err_name_index_pair **dev_err_stat = xe->hw_err_regs.dev_err_stat;
	const struct err_name_index_pair **err_stat_gt = xe->hw_err_regs.err_stat_gt;
	const struct err_name_index_pair **err_vctr_gt = xe->hw_err_regs.err_vctr_gt;
	const struct err_name_index_pair **gsc_error = xe->hw_err_regs.gsc_error;
	const struct err_name_index_pair **soc_mstr_glbl = xe->hw_err_regs.soc_mstr_glbl;
	const struct err_name_index_pair **soc_mstr_lcl = xe->hw_err_regs.soc_mstr_lcl;
	const struct err_name_index_pair **soc_slave_glbl = xe->hw_err_regs.soc_slave_glbl;
	const struct err_name_index_pair **soc_slave_lcl = xe->hw_err_regs.soc_slave_lcl;

	/* Error reporting is supported only for DG2 and PVC currently. */
	if (xe->info.platform == XE_DG2) {
		dev_err_stat[HARDWARE_ERROR_CORRECTABLE] = dg2_err_stat_correctable_reg;
		dev_err_stat[HARDWARE_ERROR_NONFATAL] = dg2_err_stat_nonfatal_reg;
		dev_err_stat[HARDWARE_ERROR_FATAL] = dg2_err_stat_fatal_reg;
		err_stat_gt[HARDWARE_ERROR_CORRECTABLE] = dg2_stat_gt_correctable_reg;
		err_stat_gt[HARDWARE_ERROR_FATAL] = dg2_stat_gt_fatal_reg;
	}

	if (xe->info.platform == XE_PVC) {
		dev_err_stat[HARDWARE_ERROR_CORRECTABLE] = pvc_err_stat_correctable_reg;
		dev_err_stat[HARDWARE_ERROR_NONFATAL] = pvc_err_stat_nonfatal_reg;
		dev_err_stat[HARDWARE_ERROR_FATAL] = pvc_err_stat_fatal_reg;
		err_stat_gt[HARDWARE_ERROR_CORRECTABLE] = pvc_err_stat_gt_correctable_reg;
		err_stat_gt[HARDWARE_ERROR_FATAL] = pvc_err_stat_gt_fatal_reg;
		err_vctr_gt[HARDWARE_ERROR_CORRECTABLE] = pvc_err_vectr_gt_correctable_reg;
		err_vctr_gt[HARDWARE_ERROR_FATAL] = pvc_err_vectr_gt_fatal_reg;
		gsc_error[HARDWARE_ERROR_CORRECTABLE] = pvc_gsc_correctable_err_reg;
		gsc_error[HARDWARE_ERROR_NONFATAL] = pvc_gsc_nonfatal_err_reg;
		soc_mstr_glbl[HARDWARE_ERROR_FATAL] = pvc_soc_mstr_glbl_err_reg_fatal;
		soc_mstr_lcl[HARDWARE_ERROR_FATAL] = pvc_soc_mstr_lcl_err_reg_fatal;
		soc_slave_glbl[HARDWARE_ERROR_FATAL] = pvc_soc_slave_glbl_err_reg_fatal;
		soc_slave_lcl[HARDWARE_ERROR_FATAL] = pvc_soc_slave_lcl_err_reg_fatal;
		soc_mstr_glbl[HARDWARE_ERROR_NONFATAL] = pvc_soc_mstr_glbl_err_reg_nonfatal;
		soc_mstr_lcl[HARDWARE_ERROR_NONFATAL] = pvc_soc_mstr_lcl_err_reg_nonfatal;
		soc_slave_glbl[HARDWARE_ERROR_NONFATAL] = pvc_soc_slave_glbl_err_reg_nonfatal;
		soc_slave_lcl[HARDWARE_ERROR_NONFATAL] = pvc_soc_slave_lcl_err_reg_nonfatal;
	}

}

static bool xe_platform_has_ras(struct xe_device *xe)
{
	if (xe->info.platform == XE_PVC || xe->info.platform == XE_DG2)
		return true;

	return false;
}

static void
xe_update_hw_error_cnt_with_value(struct drm_device *drm, struct xarray *hw_error,
				  unsigned long index, unsigned long val)
{
	unsigned long flags;
	void *entry;

	entry = xa_load(hw_error, index);
	entry = xa_mk_value(xa_to_value(entry) + val);

	xa_lock_irqsave(hw_error, flags);
	if (xa_is_err(__xa_store(hw_error, index, entry, GFP_ATOMIC)))
		drm_err_ratelimited(drm,
				    HW_ERR "Error reported by index %ld is lost\n", index);
	xa_unlock_irqrestore(hw_error, flags);
}

static void
xe_update_hw_error_cnt(struct drm_device *drm, struct xarray *hw_error, unsigned long index)
{
	xe_update_hw_error_cnt_with_value(drm, hw_error, index, 1);
}

static void
xe_gt_hw_error_log_status_reg(struct xe_gt *gt, const enum hardware_error hw_err)
{
	const char *hw_err_str = hardware_error_type_to_str(hw_err);
	const struct err_name_index_pair *errstat;
	struct hardware_errors_regs *err_regs;
	unsigned long errsrc;
	const char *name;
	u32 indx;
	u32 errbit;

	lockdep_assert_held(&gt_to_xe(gt)->irq.lock);
	err_regs = &gt_to_xe(gt)->hw_err_regs;
	errsrc = xe_mmio_read32(gt, ERR_STAT_GT_REG(hw_err));
	if (!errsrc) {
		xe_gt_log_hw_err(gt, "ERR_STAT_GT_REG_%s blank!\n", hw_err_str);
		return;
	}

	drm_dbg(&gt_to_xe(gt)->drm, HW_ERR "GT%d ERR_STAT_GT_REG_%s=0x%08lx\n",
		gt->info.id, hw_err_str, errsrc);

	if (hw_err == HARDWARE_ERROR_NONFATAL) {
		/*  The GT Non Fatal Error Status Register has only reserved bits
		 *  Nothing to service.
		 */
		xe_gt_log_hw_err(gt, "%s error\n", hw_err_str);
		goto clear_reg;
	}

	errstat = err_regs->err_stat_gt[hw_err];
	for_each_set_bit(errbit, &errsrc, XE_RAS_REG_SIZE) {
		name = errstat[errbit].name;
		indx = errstat[errbit].index;

		if (hw_err == HARDWARE_ERROR_FATAL)
			xe_gt_log_hw_err(gt, "%s %s error, bit[%d] is set\n",
					 name, hw_err_str, errbit);
		else
			xe_gt_log_hw_err(gt, "%s %s error, bit[%d] is set\n",
					 name, hw_err_str, errbit);

		xe_update_hw_error_cnt(&gt_to_xe(gt)->drm, &gt->errors.hw_error, indx);
	}
clear_reg:
	xe_mmio_write32(gt, ERR_STAT_GT_REG(hw_err), errsrc);
}

static void
xe_gt_hw_error_log_vector_reg(struct xe_gt *gt, const enum hardware_error hw_err)
{
	const char *hw_err_str = hardware_error_type_to_str(hw_err);
	const struct err_name_index_pair *errvctr;
	struct hardware_errors_regs *err_regs;
	const char *name;
	bool errstat_read;
	unsigned long val;
	u32 num_vctr_reg;
	u32 indx;
	u32 vctr;
	u32 i;

	if (hw_err == HARDWARE_ERROR_NONFATAL) {
		/*  The GT Non Fatal Error Status Register has only reserved bits
		 *  Nothing to service.
		 */
		xe_gt_log_hw_err(gt, "%s error\n", hw_err_str);
		return;
	}

	errstat_read = false;
	num_vctr_reg = ERR_STAT_GT_VCTR_LEN;
	err_regs = &gt_to_xe(gt)->hw_err_regs;
	errvctr = err_regs->err_vctr_gt[hw_err];
	for (i = 0 ; i < num_vctr_reg; i++) {
		vctr = xe_mmio_read32(gt, ERR_STAT_GT_VCTR_REG(hw_err, i));
		if (!vctr)
			continue;

		name = errvctr[i].name;
		indx = errvctr[i].index;

		if (hw_err == HARDWARE_ERROR_FATAL)
			xe_gt_log_hw_err(gt, "%s %s error. ERR_VECT_GT_%s[%d]:0x%08x\n",
					 name, hw_err_str, hw_err_str, i, vctr);
		else
			xe_gt_log_hw_warn(gt, "%s %s error. ERR_VECT_GT_%s[%d]:0x%08x\n",
					  name, hw_err_str, hw_err_str, i, vctr);

		switch (i) {
		case ERR_STAT_GT_VCTR0:
		case ERR_STAT_GT_VCTR1:
		case ERR_STAT_GT_VCTR2:
		case ERR_STAT_GT_VCTR3:
			val = hweight32(vctr);
			if (i < ERR_STAT_GT_VCTR2 && !errstat_read) {
				xe_gt_hw_error_log_status_reg(gt, hw_err);
				errstat_read = true;
			}
			xe_update_hw_error_cnt_with_value(&gt_to_xe(gt)->drm,
							  &gt->errors.hw_error, indx, val);
			break;
		case ERR_STAT_GT_VCTR6:
		case ERR_STAT_GT_VCTR7:
			val = (i == ERR_STAT_GT_VCTR6) ? hweight16(vctr) : hweight8(vctr);
			xe_update_hw_error_cnt_with_value(&gt_to_xe(gt)->drm,
							  &gt->errors.hw_error, indx, val);
			break;
		default:
			break;
		}

		xe_mmio_write32(gt, ERR_STAT_GT_VCTR_REG(hw_err, i), vctr);
	}
}

void xe_clear_all_soc_errors(struct xe_device *xe)
{
	enum hardware_error hw_err;
	u32 base, slave_base;
	struct xe_tile *tile;
	struct xe_gt *gt;
	unsigned int i;

	if (xe->info.platform != XE_PVC)
		return;

	base = SOC_PVC_BASE;
	slave_base = SOC_PVC_SLAVE_BASE;

	hw_err = HARDWARE_ERROR_CORRECTABLE;

	for_each_tile(tile, xe, i) {
		gt = tile->primary_gt;

		while (hw_err < HARDWARE_ERROR_MAX) {
			for (i = 0; i < XE_SOC_NUM_IEH; i++)
				xe_mmio_write32(gt, SOC_GSYSEVTCTL_REG(base, slave_base, i),
						~REG_BIT(hw_err));

			xe_mmio_write32(gt, SOC_GLOBAL_ERR_STAT_MASTER_REG(base, hw_err),
					REG_GENMASK(31, 0));
			xe_mmio_write32(gt, SOC_LOCAL_ERR_STAT_MASTER_REG(base, hw_err),
					REG_GENMASK(31, 0));
			xe_mmio_write32(gt, SOC_GLOBAL_ERR_STAT_SLAVE_REG(slave_base, hw_err),
					REG_GENMASK(31, 0));
			xe_mmio_write32(gt, SOC_LOCAL_ERR_STAT_SLAVE_REG(slave_base, hw_err),
					REG_GENMASK(31, 0));
			hw_err++;
		}
		for (i = 0; i < XE_SOC_NUM_IEH; i++)
			xe_mmio_write32(gt, SOC_GSYSEVTCTL_REG(base, slave_base, i),
					(HARDWARE_ERROR_MAX << 1) + 1);
	}
}

static void
xe_gt_hw_error_handler(struct xe_gt *gt, const enum hardware_error hw_err)
{
	lockdep_assert_held(&gt_to_xe(gt)->irq.lock);

	if (gt_to_xe(gt)->info.platform == XE_DG2)
		xe_gt_hw_error_log_status_reg(gt, hw_err);

	if (gt_to_xe(gt)->info.platform == XE_PVC)
		xe_gt_hw_error_log_vector_reg(gt, hw_err);
}

void xe_gsc_hw_error_work(struct work_struct *work)
{
	struct xe_tile *tile = container_of(work, typeof(*tile), gsc_hw_err_work);
	struct pci_dev *pdev = to_pci_dev(tile_to_xe(tile)->drm.dev);
	char *csc_hw_error_event[3];

	csc_hw_error_event[0] = XE_RESET_REQUIRED_UEVENT;
	csc_hw_error_event[1] = XE_RESET_REQUIRED_UEVENT_REASON_GSC;
	csc_hw_error_event[2] = NULL;

	kobject_uevent_env(&pdev->dev.kobj, KOBJ_CHANGE,
			   csc_hw_error_event);
}

static void
xe_gsc_hw_error_handler(struct xe_tile *tile, const enum hardware_error hw_err)
{
	const char *hw_err_str = hardware_error_type_to_str(hw_err);
	const struct err_name_index_pair *errstat;
	struct hardware_errors_regs *err_regs;
	struct xe_gt *gt;
	unsigned long errsrc;
	const char *name;
	u32 indx;
	u32 errbit;
	u32 base;

	if ((tile_to_xe(tile)->info.platform != XE_PVC))
		return;

	/*
	 * GSC errors are valid only on root tile and for NONFATAL and
	 * CORRECTABLE type.For non root tiles or FATAL type it should
	 * be categorized as undefined GSC HARDWARE ERROR
	 */
	base = PVC_GSC_HECI1_BASE;

	if (tile->id || hw_err == HARDWARE_ERROR_FATAL) {
		drm_err_ratelimited(&tile_to_xe(tile)->drm, HW_ERR
				    "Tile%d reported GSC %s Undefined error.\n",
				    tile->id, hw_err_str);
		return;
	}

	lockdep_assert_held(&tile_to_xe(tile)->irq.lock);
	err_regs = &tile_to_xe(tile)->hw_err_regs;
	errstat = err_regs->gsc_error[hw_err];
	gt = tile->primary_gt;
	errsrc = xe_mmio_read32(gt, GSC_HEC_ERR_STAT_REG(base, hw_err));
	if (!errsrc) {
		drm_err_ratelimited(&tile_to_xe(tile)->drm, HW_ERR
				    "Tile0 reported GSC_HEC_ERR_STAT_REG_%s blank!\n", hw_err_str);
		goto clear_reg;
	}

	drm_dbg(&tile_to_xe(tile)->drm, HW_ERR
		 "Tile0 reported GSC_HEC_ERR_STAT_REG_%s=0x%08lx\n", hw_err_str, errsrc);

	for_each_set_bit(errbit, &errsrc, XE_RAS_REG_SIZE) {
		name = errstat[errbit].name;
		indx = errstat[errbit].index;

		if (hw_err == HARDWARE_ERROR_CORRECTABLE) {
			drm_warn(&tile_to_xe(tile)->drm,
				 HW_ERR "Tile0 reported GSC %s %s error, bit[%d] is set\n",
				 name, hw_err_str, errbit);

		} else {
			drm_err_ratelimited(&tile_to_xe(tile)->drm, HW_ERR
					    "Tile0 reported GSC %s %s error, bit[%d] is set\n",
					    name, hw_err_str, errbit);
			schedule_work(&tile->gsc_hw_err_work);
		}
		if (indx != XE_HW_ERR_TILE_UNSPEC)
			xe_update_hw_error_cnt(&tile_to_xe(tile)->drm,
					       &tile->errors.hw_error, indx);
	}

clear_reg:
	xe_mmio_write32(gt, GSC_HEC_ERR_STAT_REG(base, hw_err), errsrc);
}

static void
xe_soc_log_err_update_cntr(struct xe_tile *tile, const enum hardware_error hw_err,
			   u32 errbit, const struct err_name_index_pair *reg_info)
{
	const char *name;
	u32 indx;

	const char *hwerr_to_str = hardware_error_type_to_str(hw_err);

	name = reg_info[errbit].name;
	indx = reg_info[errbit].index;

	drm_err_ratelimited(&tile_to_xe(tile)->drm, HW_ERR
			    "Tile%d reported SOC %s %s error, bit[%d] is set\n",
			    tile->id, name, hwerr_to_str, errbit);

	if (indx != XE_HW_ERR_TILE_UNSPEC)
		xe_update_hw_error_cnt(&tile_to_xe(tile)->drm, &tile->errors.hw_error, indx);
}

static void
xe_soc_hw_error_handler(struct xe_tile *tile, const enum hardware_error hw_err)
{
	unsigned long mst_glb_errstat, slv_glb_errstat, lcl_errstat;
	struct hardware_errors_regs *err_regs;
	u32 errbit, base, slave_base, ieh_header;
	int i;

	struct xe_gt *gt = tile->primary_gt;

	lockdep_assert_held(&tile_to_xe(tile)->irq.lock);

	if (tile_to_xe(tile)->info.platform != XE_PVC)
		return;

	if (hw_err == HARDWARE_ERROR_CORRECTABLE) {
		for (i = 0; i < XE_SOC_NUM_IEH; i++)
			xe_mmio_write32(gt, SOC_GSYSEVTCTL_REG(base, slave_base, i),
					~REG_BIT(hw_err));

		xe_mmio_write32(gt, SOC_GLOBAL_ERR_STAT_MASTER_REG(base, hw_err),
				REG_GENMASK(31, 0));
		xe_mmio_write32(gt, SOC_LOCAL_ERR_STAT_MASTER_REG(base, hw_err),
				REG_GENMASK(31, 0));
		xe_mmio_write32(gt, SOC_GLOBAL_ERR_STAT_SLAVE_REG(slave_base, hw_err),
				REG_GENMASK(31, 0));
		xe_mmio_write32(gt, SOC_LOCAL_ERR_STAT_SLAVE_REG(slave_base, hw_err),
				REG_GENMASK(31, 0));

		drm_err(&tile_to_xe(tile)->drm, HW_ERR
			"Tile%d reported Undefine SOC CORRECTABLE error.",
			tile->id);

		goto unmask_gsysevtctl;
	}

	base = SOC_PVC_BASE;
	slave_base = SOC_PVC_SLAVE_BASE;
	err_regs = &tile_to_xe(tile)->hw_err_regs;

	/*
	 * Mask error type in GSYSEVTCTL so that no new errors of the type
	 * will be reported. Read the master global IEH error register if
	 * BIT 1 is set then process the slave IEH first. If BIT 0 in
	 * global error register is set then process the corresponding
	 * Local error registers
	 */
	for (i = 0; i < XE_SOC_NUM_IEH; i++)
		xe_mmio_write32(gt, SOC_GSYSEVTCTL_REG(base, slave_base, i), ~REG_BIT(hw_err));

	mst_glb_errstat = xe_mmio_read32(gt, SOC_GLOBAL_ERR_STAT_MASTER_REG(base, hw_err));
	drm_dbg(&tile_to_xe(tile)->drm, HW_ERR
		 "Tile%d reported SOC_GLOBAL_ERR_STAT_MASTER_REG_FATAL:0x%08lx\n",
		 tile->id, mst_glb_errstat);

	if (mst_glb_errstat & REG_BIT(SOC_IEH1_GLOBAL_ERR_STATUS)) {
		slv_glb_errstat = xe_mmio_read32(gt,
						 SOC_GLOBAL_ERR_STAT_SLAVE_REG(slave_base, hw_err));
		 drm_dbg(&tile_to_xe(tile)->drm, HW_ERR
			  "Tile%d reported SOC_GLOBAL_ERR_STAT_SLAVE_REG_FATAL:0x%08lx\n",
			  tile->id, slv_glb_errstat);

		if (slv_glb_errstat & REG_BIT(SOC_IEH1_LOCAL_ERR_STATUS)) {
			lcl_errstat = xe_mmio_read32(gt, SOC_LOCAL_ERR_STAT_SLAVE_REG(slave_base,
										      hw_err));
			 drm_dbg(&tile_to_xe(tile)->drm, HW_ERR
				  "Tile%d reported SOC_LOCAL_ERR_STAT_SLAVE_REG_FATAL:0x%08lx\n",
				  tile->id, lcl_errstat);

			for_each_set_bit(errbit, &lcl_errstat, XE_RAS_REG_SIZE)
				xe_soc_log_err_update_cntr(tile, hw_err, errbit,
							   err_regs->soc_slave_lcl[hw_err]);

			xe_mmio_write32(gt, SOC_LOCAL_ERR_STAT_SLAVE_REG(slave_base, hw_err),
					lcl_errstat);
		}

		for_each_set_bit(errbit, &slv_glb_errstat, XE_RAS_REG_SIZE)
			xe_soc_log_err_update_cntr(tile, hw_err, errbit,
						   err_regs->soc_slave_glbl[hw_err]);

		xe_mmio_write32(gt, SOC_GLOBAL_ERR_STAT_SLAVE_REG(slave_base, hw_err),
				slv_glb_errstat);
	}

	if (mst_glb_errstat & REG_BIT(SOC_IEH0_LOCAL_ERR_STATUS)) {
		lcl_errstat = xe_mmio_read32(gt, SOC_LOCAL_ERR_STAT_MASTER_REG(base, hw_err));
		drm_dbg(&tile_to_xe(tile)->drm, HW_ERR
			"Tile%d reported SOC_LOCAL_ERR_STAT_MASTER_REG_FATAL:0x%08lx\n",
			tile->id, lcl_errstat);

		for_each_set_bit(errbit, &lcl_errstat, XE_RAS_REG_SIZE) {
			if (errbit == MDFI_T2T || errbit == MDFI_T2C) {
				ieh_header = xe_mmio_read32(gt, LOCAL_FIRST_IEH_HEADER_LOG_REG);
				drm_info(&tile_to_xe(tile)->drm, HW_ERR "Tile%d LOCAL_FIRST_IEH_HEADER_LOG_REG:0x%08x\n",
					 tile->id, ieh_header);

				if (ieh_header != MDFI_SEVERITY(hw_err)) {
					lcl_errstat &= ~REG_BIT(errbit);
					continue;
				}
			}

			xe_soc_log_err_update_cntr(tile, hw_err, errbit,
						   err_regs->soc_mstr_lcl[hw_err]);
		}

		xe_mmio_write32(gt, SOC_LOCAL_ERR_STAT_MASTER_REG(base, hw_err), lcl_errstat);
	}

	for_each_set_bit(errbit, &mst_glb_errstat, XE_RAS_REG_SIZE)
		xe_soc_log_err_update_cntr(tile, hw_err, errbit, err_regs->soc_mstr_glbl[hw_err]);

	xe_mmio_write32(gt, SOC_GLOBAL_ERR_STAT_MASTER_REG(base, hw_err),
			mst_glb_errstat);

unmask_gsysevtctl:
	for (i = 0; i < XE_SOC_NUM_IEH; i++)
		xe_mmio_write32(gt, SOC_GSYSEVTCTL_REG(base, slave_base, i),
				(HARDWARE_ERROR_MAX << 1) + 1);
}

#ifdef CONFIG_NET
static void
generate_netlink_event(struct xe_device *xe, const enum hardware_error hw_err)
{
	struct sk_buff *msg;
	void *hdr;

	if (!xe->drm.drm_genl_family.module)
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg) {
		drm_dbg_driver(&xe->drm, "couldn't allocate memory for error multicast event\n");
		return;
	}

	hdr = genlmsg_put(msg, 0, 0, &xe->drm.drm_genl_family, 0, DRM_RAS_CMD_ERROR_EVENT);
	if (!hdr) {
		drm_dbg_driver(&xe->drm, "mutlicast msg buffer is small\n");
		nlmsg_free(msg);
		return;
	}

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&xe->drm.drm_genl_family, msg, 0,
			  hw_err ?
			  DRM_GENL_MCAST_UNCORR_ERR
			  : DRM_GENL_MCAST_CORR_ERR,
			  GFP_ATOMIC);
}
#else
static void
generate_netlink_event(struct xe_device *xe, const enum hardware_error hw_err) {}
#endif

static void
xe_hw_error_source_handler(struct xe_tile *tile, const enum hardware_error hw_err)
{
	const char *hw_err_str = hardware_error_type_to_str(hw_err);
	const struct hardware_errors_regs *err_regs;
	const struct err_name_index_pair *errstat;
	unsigned long errsrc;
	unsigned long flags;
	const char *name;
	struct xe_gt *gt;
	u32 indx;
	u32 errbit;

	if (!xe_platform_has_ras(tile_to_xe(tile)))
		return;

	spin_lock_irqsave(&tile_to_xe(tile)->irq.lock, flags);
	err_regs = &tile_to_xe(tile)->hw_err_regs;
	errstat = err_regs->dev_err_stat[hw_err];
	gt = tile->primary_gt;
	errsrc = xe_mmio_read32(gt, DEV_ERR_STAT_REG(hw_err));
	if (!errsrc) {
		drm_err_ratelimited(&tile_to_xe(tile)->drm, HW_ERR
				    "TILE%d reported DEV_ERR_STAT_REG_%s blank!\n",
				    tile->id, hw_err_str);
		goto unlock;
	}

	if (tile_to_xe(tile)->info.platform != XE_DG2)
		drm_dbg(&tile_to_xe(tile)->drm, HW_ERR
			"TILE%d reported DEV_ERR_STAT_REG_%s=0x%08lx\n",
			tile->id, hw_err_str, errsrc);

	for_each_set_bit(errbit, &errsrc, XE_RAS_REG_SIZE) {
		name = errstat[errbit].name;
		indx = errstat[errbit].index;

		if (hw_err == HARDWARE_ERROR_CORRECTABLE &&
		    tile_to_xe(tile)->info.platform != XE_DG2)
			drm_warn(&tile_to_xe(tile)->drm,
				 HW_ERR "TILE%d reported %s %s error, bit[%d] is set\n",
				 tile->id, name, hw_err_str, errbit);

		else if (tile_to_xe(tile)->info.platform != XE_DG2)
			drm_err_ratelimited(&tile_to_xe(tile)->drm,
					    HW_ERR "TILE%d reported %s %s error, bit[%d] is set\n",
					    tile->id, name, hw_err_str, errbit);

		if (indx != XE_HW_ERR_TILE_UNSPEC)
			xe_update_hw_error_cnt(&tile_to_xe(tile)->drm,
					       &tile->errors.hw_error, indx);

		if (errbit == XE_GT_ERROR)
			xe_gt_hw_error_handler(tile->primary_gt, hw_err);

		if (errbit == XE_GSC_ERROR)
			xe_gsc_hw_error_handler(tile, hw_err);

		if (errbit == XE_SOC_ERROR)
			xe_soc_hw_error_handler(tile, hw_err);
	}

	xe_mmio_write32(gt, DEV_ERR_STAT_REG(hw_err), errsrc);

	generate_netlink_event(tile_to_xe(tile), hw_err);
unlock:
	spin_unlock_irqrestore(&tile_to_xe(tile)->irq.lock, flags);
}

/*
 * XE Platforms adds three Error bits to the Master Interrupt
 * Register to support error handling. These three bits are
 * used to convey the class of error:
 * FATAL, NONFATAL, or CORRECTABLE.
 *
 * To process an interrupt:
 *       Determine source of error (IP block) by reading
 *	 the Device Error Source Register (RW1C) that
 *	 corresponds to the class of error being serviced
 *	 and log the error.
 */
void
xe_hw_error_irq_handler(struct xe_tile *tile, const u32 master_ctl)
{
	enum hardware_error hw_err;

	for (hw_err = 0; hw_err < HARDWARE_ERROR_MAX; hw_err++) {
		if (master_ctl & XE_ERROR_IRQ(hw_err))
			xe_hw_error_source_handler(tile, hw_err);
	}
}

/*
 * xe_process_hw_errors - checks for the occurrence of HW errors
 *
 * Fatal will result in a card warm reset and driver will be reloaded.
 * This checks for the HW Errors that might have occurred in the
 * previous boot of the driver.
 */
void xe_process_hw_errors(struct xe_device *xe)
{
	struct xe_tile *root_tile = xe_device_get_root_tile(xe);
	struct xe_gt *root_gt = root_tile->primary_gt;

	u32 dev_pcieerr_status, master_ctl;
	struct xe_tile *tile;
	int i;

	dev_pcieerr_status = xe_mmio_read32(root_gt, DEV_PCIEERR_STATUS);

	for_each_tile(tile, xe, i) {
		struct xe_gt *gt = tile->primary_gt;

		if (dev_pcieerr_status & DEV_PCIEERR_IS_FATAL(i))
			xe_hw_error_source_handler(tile, HARDWARE_ERROR_FATAL);

		master_ctl = xe_mmio_read32(gt, GFX_MSTR_IRQ);
		xe_hw_error_irq_handler(tile, master_ctl);
		xe_mmio_write32(gt, GFX_MSTR_IRQ, master_ctl);
	}
	if (dev_pcieerr_status)
		xe_mmio_write32(root_gt, DEV_PCIEERR_STATUS, dev_pcieerr_status);
}
