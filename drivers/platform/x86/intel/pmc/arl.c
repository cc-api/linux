// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains platform specific structure definitions
 * and init function used by Meteor Lake PCH.
 *
 * Copyright (c) 2022, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include <linux/pci.h>
#include "core.h"
#include "../pmt/telemetry.h"

/* PMC SSRAM PMT Telemetry GUID */
#define SOCP_LPM_REQ_GUID	0x2625030
#define IOEP_LPM_REQ_GUID	0x5077612
#define SOCS_LPM_REQ_GUID	0x8478657
#define PCHS_LPM_REQ_GUID	0x9684572

#define PMC_DEVID_SOCM	0x7721
#define PMC_DEVID_IOEP	0x7ecf
#define PMC_DEVID_SOCS	0xae7f
#define PMC_DEVID_PCHS	0x7f27
static struct pmc_info arl_pmc_info_list[] = {
	{
		.guid	= SOCP_LPM_REQ_GUID,
		.devid	= PMC_DEVID_SOCM,
		.map	= &mtl_socm_reg_map,
	},
	{
		.guid	= IOEP_LPM_REQ_GUID,
		.devid	= PMC_DEVID_IOEP,
		.map	= &mtl_ioep_reg_map,
	},
	{
		.guid	= SOCS_LPM_REQ_GUID,
		.devid	= PMC_DEVID_SOCS,
		.map	= &mtl_socs_reg_map,
	},
	{
		.guid	= PCHS_LPM_REQ_GUID,
		.devid	= PMC_DEVID_PCHS,
		.map	= &mtl_pchs_reg_map,
	},
	{}
};

int arl_h_core_init(struct pmc_dev *pmcdev) {
	return arl_core_generic_init(pmcdev, SOC_M);
}

int arl_core_init(struct pmc_dev *pmcdev) {
	return arl_core_generic_init(pmcdev, SOC_S);
}

int arl_core_generic_init(struct pmc_dev *pmcdev, int soc_tp)
{
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_SOC];
	int ret;
	int func = 0;
	bool ssram_init = true;

	mtl_d3_fixup();

	pmcdev->resume = mtl_resume;
	pmcdev->regmap_list = arl_pmc_info_list;

	if (soc_tp == SOC_M)
		func = 2;

	/*
	 * If ssram init fails use legacy method to at least get the
	 * primary PMC
	 */
	ret = pmc_core_ssram_init(pmcdev, func);
	if (ret) {
		ssram_init = false;
		if (soc_tp == SOC_M)
			pmc->map = &mtl_socm_reg_map;
		else if (soc_tp == SOC_S)
			pmc->map = &mtl_socs_reg_map;
		else
			return -EINVAL;

		ret = get_primary_reg_base(pmc);
		if (ret)
			return ret;
	}

	pmc_core_get_low_power_modes(pmcdev);
	punit_pmt_init(pmcdev, ARL_PMT_DMU_GUID);

	/* Due to a hardware limitation, the GBE LTR blocks PC10
	 * when a cable is attached. Tell the PMC to ignore it.
	 */
	dev_dbg(&pmcdev->pdev->dev, "ignoring GBE LTR\n");
	pmc_core_send_ltr_ignore(pmcdev, 3);

	if (ssram_init)
	{
		ret = pmc_core_ssram_get_lpm_reqs(pmcdev);
		if(ret)
			return ret;
	}

	return 0;
}
