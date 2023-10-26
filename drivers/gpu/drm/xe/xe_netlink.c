// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */
#include <drm/xe_drm.h>

#include "xe_device.h"

#define MAX_ERROR_NAME	100

static const char * const xe_hw_error_events[] = {
		[XE_GENL_GT_ERROR_CORRECTABLE_L3_SNG] = "correctable-l3-sng",
		[XE_GENL_GT_ERROR_CORRECTABLE_GUC] = "correctable-guc",
		[XE_GENL_GT_ERROR_CORRECTABLE_SAMPLER] = "correctable-sampler",
		[XE_GENL_GT_ERROR_CORRECTABLE_SLM] = "correctable-slm",
		[XE_GENL_GT_ERROR_CORRECTABLE_EU_IC] = "correctable-eu-ic",
		[XE_GENL_GT_ERROR_CORRECTABLE_EU_GRF] = "correctable-eu-grf",
		[XE_GENL_GT_ERROR_FATAL_ARR_BIST] = "fatal-array-bist",
		[XE_GENL_GT_ERROR_FATAL_L3_DOUB] = "fatal-l3-double",
		[XE_GENL_GT_ERROR_FATAL_L3_ECC_CHK] = "fatal-l3-ecc-checker",
		[XE_GENL_GT_ERROR_FATAL_GUC] = "fatal-guc",
		[XE_GENL_GT_ERROR_FATAL_IDI_PAR] = "fatal-idi-parity",
		[XE_GENL_GT_ERROR_FATAL_SQIDI] = "fatal-sqidi",
		[XE_GENL_GT_ERROR_FATAL_SAMPLER] = "fatal-sampler",
		[XE_GENL_GT_ERROR_FATAL_SLM] = "fatal-slm",
		[XE_GENL_GT_ERROR_FATAL_EU_IC] = "fatal-eu-ic",
		[XE_GENL_GT_ERROR_FATAL_EU_GRF] = "fatal-eu-grf",
		[XE_GENL_GT_ERROR_FATAL_FPU] = "fatal-fpu",
		[XE_GENL_GT_ERROR_FATAL_TLB] = "fatal-tlb",
		[XE_GENL_GT_ERROR_FATAL_L3_FABRIC] = "fatal-l3-fabric",
		[XE_GENL_GT_ERROR_CORRECTABLE_SUBSLICE] = "correctable-subslice",
		[XE_GENL_GT_ERROR_CORRECTABLE_L3BANK] = "correctable-l3bank",
		[XE_GENL_GT_ERROR_FATAL_SUBSLICE] = "fatal-subslice",
		[XE_GENL_GT_ERROR_FATAL_L3BANK] = "fatal-l3bank",
		[XE_GENL_SGUNIT_ERROR_CORRECTABLE] = "sgunit-correctable",
		[XE_GENL_SGUNIT_ERROR_NONFATAL] = "sgunit-nonfatal",
		[XE_GENL_SGUNIT_ERROR_FATAL] = "sgunit-fatal",
		[XE_GENL_SOC_ERROR_NONFATAL_CSC_PSF_CMD] = "soc-nonfatal-csc-psf-cmd-parity",
		[XE_GENL_SOC_ERROR_NONFATAL_CSC_PSF_CMP] = "soc-nonfatal-csc-psf-unexpected-completion",
		[XE_GENL_SOC_ERROR_NONFATAL_CSC_PSF_REQ] = "soc-nonfatal-csc-psf-unsupported-request",
		[XE_GENL_SOC_ERROR_NONFATAL_ANR_MDFI] = "soc-nonfatal-anr-mdfi",
		[XE_GENL_SOC_ERROR_NONFATAL_MDFI_T2T] = "soc-nonfatal-mdfi-t2t",
		[XE_GENL_SOC_ERROR_NONFATAL_MDFI_T2C] = "soc-nonfatal-mdfi-t2c",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 0)] = "soc-nonfatal-hbm-ss0-0",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 1)] = "soc-nonfatal-hbm-ss0-1",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 2)] = "soc-nonfatal-hbm-ss0-2",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 3)] = "soc-nonfatal-hbm-ss0-3",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 4)] = "soc-nonfatal-hbm-ss0-4",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 5)] = "soc-nonfatal-hbm-ss0-5",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 6)] = "soc-nonfatal-hbm-ss0-6",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 7)] = "soc-nonfatal-hbm-ss0-7",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 8)] = "soc-nonfatal-hbm-ss1-0",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 9)] = "soc-nonfatal-hbm-ss1-1",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 10)] = "soc-nonfatal-hbm-ss1-2",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 11)] = "soc-nonfatal-hbm-ss1-3",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 12)] = "soc-nonfatal-hbm-ss1-4",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 13)] = "soc-nonfatal-hbm-ss1-5",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 14)] = "soc-nonfatal-hbm-ss1-6",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 15)] = "soc-nonfatal-hbm-ss1-7",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 0)] = "soc-nonfatal-hbm-ss2-0",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 1)] = "soc-nonfatal-hbm-ss2-1",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 2)] = "soc-nonfatal-hbm-ss2-2",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 3)] = "soc-nonfatal-hbm-ss2-3",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 4)] = "soc-nonfatal-hbm-ss2-4",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 5)] = "soc-nonfatal-hbm-ss2-5",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 6)] = "soc-nonfatal-hbm-ss2-6",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 7)] = "soc-nonfatal-hbm-ss2-7",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 8)] = "soc-nonfatal-hbm-ss3-0",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 9)] = "soc-nonfatal-hbm-ss3-1",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 10)] = "soc-nonfatal-hbm-ss3-2",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 11)] = "soc-nonfatal-hbm-ss3-3",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 12)] = "soc-nonfatal-hbm-ss3-4",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 13)] = "soc-nonfatal-hbm-ss3-5",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 14)] = "soc-nonfatal-hbm-ss3-6",
		[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 15)] = "soc-nonfatal-hbm-ss3-7",
		[XE_GENL_SOC_ERROR_FATAL_CSC_PSF_CMD] = "soc-fatal-csc-psf-cmd-parity",
		[XE_GENL_SOC_ERROR_FATAL_CSC_PSF_CMP] = "soc-fatal-csc-psf-unexpected-completion",
		[XE_GENL_SOC_ERROR_FATAL_CSC_PSF_REQ] = "soc-fatal-csc-psf-unsupported-request",
		[XE_GENL_SOC_ERROR_FATAL_PUNIT] = "soc-fatal-punit",
		[XE_GENL_SOC_ERROR_FATAL_PCIE_PSF_CMD] = "soc-fatal-pcie-psf-command-parity",
		[XE_GENL_SOC_ERROR_FATAL_PCIE_PSF_CMP] = "soc-fatal-pcie-psf-unexpected-completion",
		[XE_GENL_SOC_ERROR_FATAL_PCIE_PSF_REQ] = "soc-fatal-pcie-psf-unsupported-request",
		[XE_GENL_SOC_ERROR_FATAL_ANR_MDFI] = "soc-fatal-anr-mdfi",
		[XE_GENL_SOC_ERROR_FATAL_MDFI_T2T] = "soc-fatal-mdfi-t2t",
		[XE_GENL_SOC_ERROR_FATAL_MDFI_T2C] = "soc-fatal-mdfi-t2c",
		[XE_GENL_SOC_ERROR_FATAL_PCIE_AER] = "soc-fatal-malformed-pcie-aer",
		[XE_GENL_SOC_ERROR_FATAL_PCIE_ERR] = "soc-fatal-malformed-pcie-err",
		[XE_GENL_SOC_ERROR_FATAL_UR_COND] = "soc-fatal-ur-condition-ieh",
		[XE_GENL_SOC_ERROR_FATAL_SERR_SRCS] = "soc-fatal-from-serr-sources",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 0)] = "soc-fatal-hbm-ss0-0",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 1)] = "soc-fatal-hbm-ss0-1",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 2)] = "soc-fatal-hbm-ss0-2",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 3)] = "soc-fatal-hbm-ss0-3",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 4)] = "soc-fatal-hbm-ss0-4",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 5)] = "soc-fatal-hbm-ss0-5",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 6)] = "soc-fatal-hbm-ss0-6",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 7)] = "soc-fatal-hbm-ss0-7",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 8)] = "soc-fatal-hbm-ss1-0",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 9)] = "soc-fatal-hbm-ss1-1",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 10)] = "soc-fatal-hbm-ss1-2",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 11)] = "soc-fatal-hbm-ss1-3",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 12)] = "soc-fatal-hbm-ss1-4",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 13)] = "soc-fatal-hbm-ss1-5",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 14)] = "soc-fatal-hbm-ss1-6",
		[XE_GENL_SOC_ERROR_FATAL_HBM(0, 15)] = "soc-fatal-hbm-ss1-7",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 0)] = "soc-fatal-hbm-ss2-0",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 1)] = "soc-fatal-hbm-ss2-1",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 2)] = "soc-fatal-hbm-ss2-2",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 3)] = "soc-fatal-hbm-ss2-3",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 4)] = "soc-fatal-hbm-ss2-4",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 5)] = "soc-fatal-hbm-ss2-5",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 6)] = "soc-fatal-hbm-ss2-6",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 7)] = "soc-fatal-hbm-ss2-7",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 8)] = "soc-fatal-hbm-ss3-0",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 9)] = "soc-fatal-hbm-ss3-1",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 10)] = "soc-fatal-hbm-ss3-2",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 11)] = "soc-fatal-hbm-ss3-3",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 12)] = "soc-fatal-hbm-ss3-4",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 13)] = "soc-fatal-hbm-ss3-5",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 14)] = "soc-fatal-hbm-ss3-6",
		[XE_GENL_SOC_ERROR_FATAL_HBM(1, 15)] = "soc-fatal-hbm-ss3-7",
		[XE_GENL_GSC_ERROR_CORRECTABLE_SRAM_ECC] = "gsc-correctable-sram-ecc",
		[XE_GENL_GSC_ERROR_NONFATAL_MIA_SHUTDOWN] = "gsc-nonfatal-mia-shutdown",
		[XE_GENL_GSC_ERROR_NONFATAL_MIA_INTERNAL] = "gsc-nonfatal-mia-internal",
		[XE_GENL_GSC_ERROR_NONFATAL_SRAM_ECC] = "gsc-nonfatal-sram-ecc",
		[XE_GENL_GSC_ERROR_NONFATAL_WDG_TIMEOUT] = "gsc-nonfatal-wdg-timeout",
		[XE_GENL_GSC_ERROR_NONFATAL_ROM_PARITY] = "gsc-nonfatal-rom-parity",
		[XE_GENL_GSC_ERROR_NONFATAL_UCODE_PARITY] = "gsc-nonfatal-ucode-parity",
		[XE_GENL_GSC_ERROR_NONFATAL_VLT_GLITCH] = "gsc-nonfatal-vlt-glitch",
		[XE_GENL_GSC_ERROR_NONFATAL_FUSE_PULL] = "gsc-nonfatal-fuse-pull",
		[XE_GENL_GSC_ERROR_NONFATAL_FUSE_CRC_CHECK] = "gsc-nonfatal-fuse-crc-check",
		[XE_GENL_GSC_ERROR_NONFATAL_SELF_MBIST] = "gsc-nonfatal-self-mbist",
		[XE_GENL_GSC_ERROR_NONFATAL_AON_RF_PARITY] = "gsc-nonfatal-aon-parity",
		[XE_GENL_SGGI_ERROR_NONFATAL] = "sggi-nonfatal-data-parity",
		[XE_GENL_SGLI_ERROR_NONFATAL] = "sgli-nonfatal-data-parity",
		[XE_GENL_SGCI_ERROR_NONFATAL] = "sgci-nonfatal-data-parity",
		[XE_GENL_MERT_ERROR_NONFATAL] = "mert-nonfatal-data-parity",
		[XE_GENL_SGGI_ERROR_FATAL] = "sggi-fatal-data-parity",
		[XE_GENL_SGLI_ERROR_FATAL] = "sgli-fatal-data-parity",
		[XE_GENL_SGCI_ERROR_FATAL] = "sgci-fatal-data-parity",
		[XE_GENL_MERT_ERROR_FATAL] = "mert-nonfatal-data-parity",
};

static const unsigned long xe_hw_error_map[] = {
	[XE_GENL_GT_ERROR_CORRECTABLE_L3_SNG] = XE_HW_ERR_GT_CORR_L3_SNG,
	[XE_GENL_GT_ERROR_CORRECTABLE_GUC] = XE_HW_ERR_GT_CORR_GUC,
	[XE_GENL_GT_ERROR_CORRECTABLE_SAMPLER] = XE_HW_ERR_GT_CORR_SAMPLER,
	[XE_GENL_GT_ERROR_CORRECTABLE_SLM] = XE_HW_ERR_GT_CORR_SLM,
	[XE_GENL_GT_ERROR_CORRECTABLE_EU_IC] = XE_HW_ERR_GT_CORR_EU_IC,
	[XE_GENL_GT_ERROR_CORRECTABLE_EU_GRF] = XE_HW_ERR_GT_CORR_EU_GRF,
	[XE_GENL_GT_ERROR_FATAL_ARR_BIST] = XE_HW_ERR_GT_FATAL_ARR_BIST,
	[XE_GENL_GT_ERROR_FATAL_L3_DOUB] = XE_HW_ERR_GT_FATAL_L3_DOUB,
	[XE_GENL_GT_ERROR_FATAL_L3_ECC_CHK] = XE_HW_ERR_GT_FATAL_L3_ECC_CHK,
	[XE_GENL_GT_ERROR_FATAL_GUC] = XE_HW_ERR_GT_FATAL_GUC,
	[XE_GENL_GT_ERROR_FATAL_IDI_PAR] = XE_HW_ERR_GT_FATAL_IDI_PAR,
	[XE_GENL_GT_ERROR_FATAL_SQIDI] = XE_HW_ERR_GT_FATAL_SQIDI,
	[XE_GENL_GT_ERROR_FATAL_SAMPLER] = XE_HW_ERR_GT_FATAL_SAMPLER,
	[XE_GENL_GT_ERROR_FATAL_SLM] = XE_HW_ERR_GT_FATAL_SLM,
	[XE_GENL_GT_ERROR_FATAL_EU_IC] = XE_HW_ERR_GT_FATAL_EU_IC,
	[XE_GENL_GT_ERROR_FATAL_EU_GRF] = XE_HW_ERR_GT_FATAL_EU_GRF,
	[XE_GENL_GT_ERROR_FATAL_FPU] = XE_HW_ERR_GT_FATAL_FPU,
	[XE_GENL_GT_ERROR_FATAL_TLB] = XE_HW_ERR_GT_FATAL_TLB,
	[XE_GENL_GT_ERROR_FATAL_L3_FABRIC] = XE_HW_ERR_GT_FATAL_L3_FABRIC,
	[XE_GENL_GT_ERROR_CORRECTABLE_SUBSLICE] = XE_HW_ERR_GT_CORR_SUBSLICE,
	[XE_GENL_GT_ERROR_CORRECTABLE_L3BANK] = XE_HW_ERR_GT_CORR_L3BANK,
	[XE_GENL_GT_ERROR_FATAL_SUBSLICE] = XE_HW_ERR_GT_FATAL_SUBSLICE,
	[XE_GENL_GT_ERROR_FATAL_L3BANK] = XE_HW_ERR_GT_FATAL_L3BANK,
	[XE_GENL_SGUNIT_ERROR_CORRECTABLE] = XE_HW_ERR_TILE_CORR_SGUNIT,
	[XE_GENL_SGUNIT_ERROR_NONFATAL] = XE_HW_ERR_TILE_NONFATAL_SGUNIT,
	[XE_GENL_SGUNIT_ERROR_FATAL] = XE_HW_ERR_TILE_FATAL_SGUNIT,
	[XE_GENL_SOC_ERROR_NONFATAL_CSC_PSF_CMD] = XE_HW_ERR_SOC_NONFATAL_CSC_PSF_CMD,
	[XE_GENL_SOC_ERROR_NONFATAL_CSC_PSF_CMP] = XE_HW_ERR_SOC_NONFATAL_CSC_PSF_CMP,
	[XE_GENL_SOC_ERROR_NONFATAL_CSC_PSF_REQ] = XE_HW_ERR_SOC_NONFATAL_CSC_PSF_REQ,
	[XE_GENL_SOC_ERROR_NONFATAL_ANR_MDFI] = XE_HW_ERR_SOC_NONFATAL_ANR_MDFI,
	[XE_GENL_SOC_ERROR_NONFATAL_MDFI_T2T] = XE_HW_ERR_SOC_NONFATAL_MDFI_T2T,
	[XE_GENL_SOC_ERROR_NONFATAL_MDFI_T2C] = XE_HW_ERR_SOC_NONFATAL_MDFI_T2C,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 0)] = XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL0,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 1)] = XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL1,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 2)] = XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL2,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 3)] = XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL3,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 4)] = XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL4,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 5)] = XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL5,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 6)] = XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL6,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 7)] = XE_HW_ERR_SOC_NONFATAL_HBM0_CHNL7,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 8)] = XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL0,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 9)] = XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL1,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 10)] = XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL2,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 11)] = XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL3,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 12)] = XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL4,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 13)] = XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL5,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 14)] = XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL6,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(0, 15)] = XE_HW_ERR_SOC_NONFATAL_HBM1_CHNL7,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 0)] = XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL0,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 1)] = XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL1,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 2)] = XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL2,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 3)] = XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL3,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 4)] = XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL4,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 5)] = XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL5,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 6)] = XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL6,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 7)] = XE_HW_ERR_SOC_NONFATAL_HBM2_CHNL7,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 8)] = XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL0,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 9)] = XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL1,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 10)] = XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL2,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 11)] = XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL3,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 12)] = XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL4,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 13)] = XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL5,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 14)] = XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL6,
	[XE_GENL_SOC_ERROR_NONFATAL_HBM(1, 15)] = XE_HW_ERR_SOC_NONFATAL_HBM3_CHNL7,
	[XE_GENL_SOC_ERROR_FATAL_CSC_PSF_CMD] = XE_HW_ERR_SOC_FATAL_CSC_PSF_CMD,
	[XE_GENL_SOC_ERROR_FATAL_CSC_PSF_CMP] = XE_HW_ERR_SOC_FATAL_CSC_PSF_CMP,
	[XE_GENL_SOC_ERROR_FATAL_CSC_PSF_REQ] = XE_HW_ERR_SOC_FATAL_CSC_PSF_REQ,
	[XE_GENL_SOC_ERROR_FATAL_PUNIT] = XE_HW_ERR_SOC_FATAL_PUNIT,
	[XE_GENL_SOC_ERROR_FATAL_PCIE_PSF_CMD] = XE_HW_ERR_SOC_FATAL_PCIE_PSF_CMD,
	[XE_GENL_SOC_ERROR_FATAL_PCIE_PSF_CMP] = XE_HW_ERR_SOC_FATAL_PCIE_PSF_CMP,
	[XE_GENL_SOC_ERROR_FATAL_PCIE_PSF_REQ] = XE_HW_ERR_SOC_FATAL_PCIE_PSF_REQ,
	[XE_GENL_SOC_ERROR_FATAL_ANR_MDFI] = XE_HW_ERR_SOC_FATAL_ANR_MDFI,
	[XE_GENL_SOC_ERROR_FATAL_MDFI_T2T] = XE_HW_ERR_SOC_FATAL_MDFI_T2T,
	[XE_GENL_SOC_ERROR_FATAL_MDFI_T2C] = XE_HW_ERR_SOC_FATAL_MDFI_T2C,
	[XE_GENL_SOC_ERROR_FATAL_PCIE_AER] = XE_HW_ERR_SOC_FATAL_PCIE_AER,
	[XE_GENL_SOC_ERROR_FATAL_PCIE_ERR] = XE_HW_ERR_SOC_FATAL_PCIE_ERR,
	[XE_GENL_SOC_ERROR_FATAL_UR_COND] = XE_HW_ERR_SOC_FATAL_UR_COND,
	[XE_GENL_SOC_ERROR_FATAL_SERR_SRCS] = XE_HW_ERR_SOC_FATAL_SERR_SRCS,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 0)] = XE_HW_ERR_SOC_FATAL_HBM0_CHNL0,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 1)] = XE_HW_ERR_SOC_FATAL_HBM0_CHNL1,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 2)] = XE_HW_ERR_SOC_FATAL_HBM0_CHNL2,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 3)] = XE_HW_ERR_SOC_FATAL_HBM0_CHNL3,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 4)] = XE_HW_ERR_SOC_FATAL_HBM0_CHNL4,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 5)] = XE_HW_ERR_SOC_FATAL_HBM0_CHNL5,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 6)] = XE_HW_ERR_SOC_FATAL_HBM0_CHNL6,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 7)] = XE_HW_ERR_SOC_FATAL_HBM0_CHNL7,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 8)] = XE_HW_ERR_SOC_FATAL_HBM1_CHNL0,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 9)] = XE_HW_ERR_SOC_FATAL_HBM1_CHNL1,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 10)] = XE_HW_ERR_SOC_FATAL_HBM1_CHNL2,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 11)] = XE_HW_ERR_SOC_FATAL_HBM1_CHNL3,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 12)] = XE_HW_ERR_SOC_FATAL_HBM1_CHNL4,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 13)] = XE_HW_ERR_SOC_FATAL_HBM1_CHNL5,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 14)] = XE_HW_ERR_SOC_FATAL_HBM1_CHNL6,
	[XE_GENL_SOC_ERROR_FATAL_HBM(0, 15)] = XE_HW_ERR_SOC_FATAL_HBM1_CHNL7,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 0)] = XE_HW_ERR_SOC_FATAL_HBM2_CHNL0,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 1)] = XE_HW_ERR_SOC_FATAL_HBM2_CHNL1,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 2)] = XE_HW_ERR_SOC_FATAL_HBM2_CHNL2,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 3)] = XE_HW_ERR_SOC_FATAL_HBM2_CHNL3,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 4)] = XE_HW_ERR_SOC_FATAL_HBM2_CHNL4,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 5)] = XE_HW_ERR_SOC_FATAL_HBM2_CHNL5,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 6)] = XE_HW_ERR_SOC_FATAL_HBM2_CHNL6,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 7)] = XE_HW_ERR_SOC_FATAL_HBM2_CHNL7,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 8)] = XE_HW_ERR_SOC_FATAL_HBM3_CHNL0,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 9)] = XE_HW_ERR_SOC_FATAL_HBM3_CHNL1,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 10)] = XE_HW_ERR_SOC_FATAL_HBM3_CHNL2,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 11)] = XE_HW_ERR_SOC_FATAL_HBM3_CHNL3,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 12)] = XE_HW_ERR_SOC_FATAL_HBM3_CHNL4,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 13)] = XE_HW_ERR_SOC_FATAL_HBM3_CHNL5,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 14)] = XE_HW_ERR_SOC_FATAL_HBM3_CHNL6,
	[XE_GENL_SOC_ERROR_FATAL_HBM(1, 15)] = XE_HW_ERR_SOC_FATAL_HBM3_CHNL7,
	[XE_GENL_GSC_ERROR_CORRECTABLE_SRAM_ECC] = XE_HW_ERR_GSC_CORR_SRAM,
	[XE_GENL_GSC_ERROR_NONFATAL_MIA_SHUTDOWN] = XE_HW_ERR_GSC_NONFATAL_MIA_SHUTDOWN,
	[XE_GENL_GSC_ERROR_NONFATAL_MIA_INTERNAL] = XE_HW_ERR_GSC_NONFATAL_MIA_INTERNAL,
	[XE_GENL_GSC_ERROR_NONFATAL_SRAM_ECC] = XE_HW_ERR_GSC_NONFATAL_SRAM,
	[XE_GENL_GSC_ERROR_NONFATAL_WDG_TIMEOUT] = XE_HW_ERR_GSC_NONFATAL_WDG,
	[XE_GENL_GSC_ERROR_NONFATAL_ROM_PARITY] = XE_HW_ERR_GSC_NONFATAL_ROM_PARITY,
	[XE_GENL_GSC_ERROR_NONFATAL_UCODE_PARITY] = XE_HW_ERR_GSC_NONFATAL_UCODE_PARITY,
	[XE_GENL_GSC_ERROR_NONFATAL_VLT_GLITCH] = XE_HW_ERR_GSC_NONFATAL_VLT_GLITCH,
	[XE_GENL_GSC_ERROR_NONFATAL_FUSE_PULL] = XE_HW_ERR_GSC_NONFATAL_FUSE_PULL,
	[XE_GENL_GSC_ERROR_NONFATAL_FUSE_CRC_CHECK] = XE_HW_ERR_GSC_NONFATAL_FUSE_CRC,
	[XE_GENL_GSC_ERROR_NONFATAL_SELF_MBIST] = XE_HW_ERR_GSC_NONFATAL_SELF_MBIST,
	[XE_GENL_GSC_ERROR_NONFATAL_AON_RF_PARITY] = XE_HW_ERR_GSC_NONFATAL_AON_RF_PARITY,
	[XE_GENL_SGGI_ERROR_NONFATAL] = XE_HW_ERR_TILE_NONFATAL_SGGI,
	[XE_GENL_SGLI_ERROR_NONFATAL] = XE_HW_ERR_TILE_NONFATAL_SGLI,
	[XE_GENL_SGCI_ERROR_NONFATAL] = XE_HW_ERR_TILE_NONFATAL_SGCI,
	[XE_GENL_MERT_ERROR_NONFATAL] = XE_HW_ERR_TILE_NONFATAL_MERT,
	[XE_GENL_SGGI_ERROR_FATAL] = XE_HW_ERR_TILE_FATAL_SGGI,
	[XE_GENL_SGLI_ERROR_FATAL] = XE_HW_ERR_TILE_FATAL_SGLI,
	[XE_GENL_SGCI_ERROR_FATAL] = XE_HW_ERR_TILE_FATAL_SGCI,
	[XE_GENL_MERT_ERROR_FATAL] = XE_HW_ERR_TILE_FATAL_MERT,
};

static unsigned int config_gt_id(const u64 config)
{
	return config >> __XE_PMU_GT_SHIFT;
}

static u64 config_counter(const u64 config)
{
	return config & ~(~0ULL << __XE_PMU_GT_SHIFT);
}

static bool is_gt_error(const u64 config)
{
	unsigned int error;

	error = config_counter(config);
	if (error <= XE_GENL_GT_ERROR_FATAL_FPU)
		return true;

	return false;
}

static bool is_gt_vector_error(const u64 config)
{
	unsigned int error;

	error = config_counter(config);
	if (error >= XE_GENL_GT_ERROR_FATAL_TLB &&
	    error <= XE_GENL_GT_ERROR_FATAL_L3BANK)
		return true;

	return false;
}

static bool is_pvc_invalid_gt_errors(const u64 config)
{
	switch (config_counter(config)) {
	case XE_GENL_GT_ERROR_CORRECTABLE_L3_SNG:
	case XE_GENL_GT_ERROR_CORRECTABLE_SAMPLER:
	case XE_GENL_GT_ERROR_FATAL_ARR_BIST:
	case XE_GENL_GT_ERROR_FATAL_L3_DOUB:
	case XE_GENL_GT_ERROR_FATAL_L3_ECC_CHK:
	case XE_GENL_GT_ERROR_FATAL_IDI_PAR:
	case XE_GENL_GT_ERROR_FATAL_SQIDI:
	case XE_GENL_GT_ERROR_FATAL_SAMPLER:
	case XE_GENL_GT_ERROR_FATAL_EU_IC:
		return true;
	default:
		return false;
	}
}

static bool is_gsc_hw_error(const u64 config)
{
	if (config_counter(config) >= XE_GENL_GSC_ERROR_CORRECTABLE_SRAM_ECC &&
	    config_counter(config) <= XE_GENL_GSC_ERROR_NONFATAL_AON_RF_PARITY)
		return true;

	return false;
}

static bool is_soc_error(const u64 config)
{
	if (config_counter(config) >= XE_GENL_SOC_ERROR_NONFATAL_CSC_PSF_CMD &&
	    config_counter(config) <= XE_GENL_SOC_ERROR_FATAL_HBM(1, 15))
		return true;

	return false;
}

static int
config_status(struct xe_device *xe, u64 config)
{
	unsigned int gt_id = config_gt_id(config);
	struct xe_gt *gt = xe_device_get_gt(xe, gt_id);

	if (!IS_DGFX(xe))
		return -ENODEV;

	if (gt->info.type == XE_GT_TYPE_UNINITIALIZED)
		return -ENOENT;

	/* GSC HW ERRORS are present on root tile of
	 * platform supporting MEMORY SPARING only
	 */
	if (is_gsc_hw_error(config) && !(xe->info.platform == XE_PVC && !gt_id))
		return -ENODEV;

	/* GT vectors error  are valid on Platforms supporting error vectors only */
	if (is_gt_vector_error(config) && xe->info.platform != XE_PVC)
		return -ENODEV;

	/* Skip gt errors not supported on pvc */
	if (is_pvc_invalid_gt_errors(config) && xe->info.platform == XE_PVC)
		return  -ENODEV;

	/* FATAL FPU error is valid on PVC only */
	if (config_counter(config) == XE_GENL_GT_ERROR_FATAL_FPU &&
	    !(xe->info.platform == XE_PVC))
		return -ENODEV;

	if (is_soc_error(config) && !(xe->info.platform == XE_PVC))
		return -ENODEV;

	return (config_counter(config) >=
			ARRAY_SIZE(xe_hw_error_map)) ? -ENOENT : 0;
}

static u64 get_counter_value(struct xe_device *xe, u64 config)
{
	const unsigned int gt_id = config_gt_id(config);
	struct xe_gt *gt = xe_device_get_gt(xe, gt_id);
	unsigned int id = config_counter(config);

	if (is_gt_error(config) || is_gt_vector_error(config))
		return xa_to_value(xa_load(&gt->errors.hw_error, xe_hw_error_map[id]));

	return xa_to_value(xa_load(&gt->tile->errors.hw_error, xe_hw_error_map[id]));
}

int fill_error_details(struct xe_device *xe, struct genl_info *info, struct sk_buff *new_msg)
{
	struct nlattr *entry_attr;
	bool counter = false;
	struct xe_gt *gt;
	int i, j;

	BUILD_BUG_ON(ARRAY_SIZE(xe_hw_error_events) !=
		     ARRAY_SIZE(xe_hw_error_map));

	if (info->genlhdr->cmd == DRM_RAS_CMD_READ_ALL)
		counter = true;

	entry_attr = nla_nest_start(new_msg, DRM_RAS_ATTR_QUERY_REPLY);
	if (!entry_attr)
		return -EMSGSIZE;

	for_each_gt(gt, xe, j) {
		char str[MAX_ERROR_NAME];
		u64 val;

		for (i = 0; i < ARRAY_SIZE(xe_hw_error_events); i++) {
			u64 config = XE_HW_ERROR(j, i);

			if (config_status(xe, config))
				continue;

			/* should this be cleared everytime */
			snprintf(str, sizeof(str), "error-gt%d-%s", j, xe_hw_error_events[i]);

			if (nla_put_string(new_msg, DRM_RAS_ATTR_ERROR_NAME, str))
				goto err;
			if (nla_put_u64_64bit(new_msg, DRM_RAS_ATTR_ERROR_ID, config, DRM_ATTR_PAD))
				goto err;
			if (counter) {
				val = get_counter_value(xe, config);
				if (nla_put_u64_64bit(new_msg, DRM_RAS_ATTR_ERROR_VALUE, val, DRM_ATTR_PAD))
					goto err;
			}
		}
	}

	nla_nest_end(new_msg, entry_attr);

	return 0;
err:
	drm_dbg_driver(&xe->drm, "msg buff is small\n");
	nla_nest_cancel(new_msg, entry_attr);
	nlmsg_free(new_msg);

	return -EMSGSIZE;
}

static int xe_genl_list_errors(struct drm_device *drm, struct sk_buff *msg, struct genl_info *info)
{
	struct xe_device *xe = to_xe_device(drm);
	size_t msg_size = NLMSG_DEFAULT_SIZE;
	struct sk_buff *new_msg;
	int retries = 2;
	void *usrhdr;
	int ret = 0;

	if (!IS_DGFX(xe))
		return -ENODEV;

	do {
		new_msg = drm_genl_alloc_msg(drm, info, msg_size, &usrhdr);
		if (!new_msg)
			return -ENOMEM;

		ret = fill_error_details(xe, info, new_msg);
		if (!ret)
			break;

		msg_size += NLMSG_DEFAULT_SIZE;
	} while (retries--);

	if (!ret)
		ret = drm_genl_reply(new_msg, info, usrhdr);

	return ret;
}

static int xe_genl_read_error(struct drm_device *drm, struct sk_buff *msg, struct genl_info *info)
{
	struct xe_device *xe = to_xe_device(drm);
	size_t msg_size = NLMSG_DEFAULT_SIZE;
	struct sk_buff *new_msg;
	void *usrhdr;
	int ret = 0;
	int retries = 2;
	u64 config, val;

	config = nla_get_u64(info->attrs[DRM_RAS_ATTR_ERROR_ID]);
	ret = config_status(xe, config);
	if (ret)
		return ret;
	do {
		new_msg = drm_genl_alloc_msg(drm, info, msg_size, &usrhdr);
		if (!new_msg)
			return -ENOMEM;

		val = get_counter_value(xe, config);
		if (nla_put_u64_64bit(new_msg, DRM_RAS_ATTR_ERROR_VALUE, val, DRM_ATTR_PAD)) {
			msg_size += NLMSG_DEFAULT_SIZE;
			continue;
		}

		break;
	} while (retries--);

	ret = drm_genl_reply(new_msg, info, usrhdr);

	return ret;
}

/* driver callbacks to DRM netlink commands*/
const struct driver_genl_ops xe_genl_ops[] = {
	[DRM_RAS_CMD_QUERY] =		{ .doit = xe_genl_list_errors },
	[DRM_RAS_CMD_READ_ONE] =	{ .doit = xe_genl_read_error },
	[DRM_RAS_CMD_READ_ALL] =	{ .doit = xe_genl_list_errors, },
};
