// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains functions to handle discovery of PMC metrics located
 * in the PMC SSRAM PCI device.
 *
 * Copyright (c) 2023, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include <linux/pci.h>
#include <linux/io-64-nonatomic-lo-hi.h>

#include "core.h"
#include "../vsec.h"
#include "../pmt/telemetry.h"

#define SSRAM_HDR_SIZE		0x100
#define SSRAM_PWRM_OFFSET	0x14
#define SSRAM_DVSEC_OFFSET	0x1C
#define SSRAM_DVSEC_SIZE	0x10
#define SSRAM_PCH_OFFSET	0x60
#define SSRAM_IOE_OFFSET	0x68
#define SSRAM_DEVID_OFFSET	0x70

/* PCH query */
#define LPM_REG_INDEX_OFFSET	2
#define LPM_REG_NUM		28
#define LPM_SUBSTATE_NUM	1

static u32 pmc_core_find_guid(struct pmc_info *list, const struct pmc_reg_map *map)
{
	for (; list->map; ++list)
		if (list->map == map)
			return list->guid;

	return 0;
}

int pmc_core_get_lpm_reqs(struct pmc_dev *pmcdev)
{
	struct pci_dev *pcidev;
	int i, j, mode, pmc_index;
	u32 *lpm_req_regs, guid, lpm_size;

	for (pmc_index = 0; pmc_index < ARRAY_SIZE(pmcdev->pmcs); ++pmc_index) {
		struct pmc *pmc = pmcdev->pmcs[pmc_index];
		int map_offset = 0;
		int ret = 0;
		const u8 *reg_index;
		int num_maps;
		struct telem_endpoint *ep;

		if (!pmc)
			continue;

		reg_index = pmc->map->lpm_reg_index;
		num_maps = pmc->map->lpm_num_maps;
		lpm_size = LPM_MAX_NUM_MODES * num_maps;
		pcidev = pmcdev->ssram_pcidev;
		pmc->lpm_req_regs = NULL;

		if (!pcidev)
			return 0;

		guid = pmc_core_find_guid(pmcdev->regmap_list, pmc->map);
		if (!guid)
			return 0;

		ep = pmt_telem_find_and_register_endpoint(pcidev, guid, 0);
		if (IS_ERR(ep)) {
			ret = PTR_ERR(ep);
			dev_err(&pmcdev->pdev->dev, "pmc_core: couldn't get telem endpoint %d", ret);
			return -EPROBE_DEFER;
		}

		lpm_req_regs = devm_kzalloc(&pmcdev->pdev->dev, lpm_size * sizeof(u32),
					     GFP_KERNEL);

		for (j = 0, mode = pmcdev->lpm_en_modes[j]; j < pmcdev->num_lpm_modes; j++,
			 mode = pmcdev->lpm_en_modes[j]) {
			u32 *ptr;

			ptr = lpm_req_regs;
			ptr += mode * num_maps;
			for (i = 0; i < num_maps; ++i) {
				u8 index = reg_index[i] + LPM_REG_INDEX_OFFSET + map_offset;

				ret = pmt_telem_read32(ep, index, ptr, 1);
				if (ret) {
					dev_err(&pmcdev->pdev->dev,
							"pmc_core: couldn't read 32 bit data %d", ret);
					return 0;
				}
				++ptr;
			}
			map_offset += LPM_REG_NUM + LPM_SUBSTATE_NUM;
		}
		pmc->lpm_req_regs = lpm_req_regs;
		pmt_telem_unregister_endpoint(ep);
	}
	return 0;
}

static void
pmc_add_pmt(struct pmc_dev *pmcdev, u64 ssram_base, void __iomem *ssram)
{
	struct pci_dev *pcidev = pmcdev->ssram_pcidev;
	struct intel_vsec_platform_info info = {};
	struct intel_vsec_header *headers[2] = {};
	struct intel_vsec_header header;
	void __iomem *dvsec;
	u32 dvsec_offset;
	u32 table, hdr;

	ssram = ioremap(ssram_base, SSRAM_HDR_SIZE);
	if (!ssram)
		return;

	dvsec_offset = readl(ssram + SSRAM_DVSEC_OFFSET);
	iounmap(ssram);

	dvsec = ioremap(ssram_base + dvsec_offset, SSRAM_DVSEC_SIZE);
	if (!dvsec)
		return;

	hdr = readl(dvsec + PCI_DVSEC_HEADER1);
	header.id = readw(dvsec + PCI_DVSEC_HEADER2);
	header.rev = PCI_DVSEC_HEADER1_REV(hdr);
	header.length = PCI_DVSEC_HEADER1_LEN(hdr);
	header.num_entries = readb(dvsec + INTEL_DVSEC_ENTRIES);
	header.entry_size = readb(dvsec + INTEL_DVSEC_SIZE);

	table = readl(dvsec + INTEL_DVSEC_TABLE);
	header.tbir = INTEL_DVSEC_TABLE_BAR(table);
	header.offset = INTEL_DVSEC_TABLE_OFFSET(table);
	iounmap(dvsec);

	headers[0] = &header;
	info.caps = VSEC_CAP_TELEMETRY;
	info.headers = headers;
	info.base_addr = ssram_base;
	info.parent = &pmcdev->pdev->dev;

	intel_vsec_register(pcidev, &info);
}

static const struct pmc_reg_map *pmc_core_find_regmap(struct pmc_info *list, u16 devid)
{
	for (; list->map; ++list)
		if (devid == list->devid)
			return list->map;

	return NULL;
}

static inline u64 get_base(void __iomem *addr, u32 offset)
{
	return lo_hi_readq(addr + offset) & GENMASK_ULL(63, 3);
}

static int
pmc_core_pmc_add(struct pmc_dev *pmcdev, u64 pwrm_base,
		 const struct pmc_reg_map *reg_map, int pmc_index)
{
	struct pmc *pmc = pmcdev->pmcs[pmc_index];

	if (!pwrm_base)
		return -ENODEV;

	/* Memory for primary PMC has been allocated in core.c */
	if (!pmc) {
		pmc = devm_kzalloc(&pmcdev->pdev->dev, sizeof(*pmc), GFP_KERNEL);
		if (!pmc)
			return -ENOMEM;
	}

	pmc->map = reg_map;
	pmc->base_addr = pwrm_base;
	pmc->regbase = ioremap(pmc->base_addr, pmc->map->regmap_length);

	if (!pmc->regbase) {
		devm_kfree(&pmcdev->pdev->dev, pmc);
		return -ENOMEM;
	}

	pmcdev->pmcs[pmc_index] = pmc;

	return 0;
}

static int
pmc_core_get_secondary_pmc(struct pmc_dev *pmcdev, int pmc_idx, u32 offset)
{
	struct pci_dev *ssram_pcidev = pmcdev->ssram_pcidev;
	const struct pmc_reg_map *map;
	void __iomem *main_ssram, *secondary_ssram;
	u64 ssram_base, pwrm_base;
	u16 devid;
	int ret;

	if (!pmcdev->regmap_list)
		return -ENOENT;

	/*
	 * The secondary PMC BARS (which are behind hidden PCI devices) are read
	 * from fixed offsets in MMIO of the primary PMC BAR.
	 */
	ssram_base = ssram_pcidev->resource[0].start;
	main_ssram = ioremap(ssram_base, SSRAM_HDR_SIZE);
	if (!main_ssram)
		return -ENOMEM;

	ssram_base = get_base(main_ssram, offset);
	secondary_ssram = ioremap(ssram_base, SSRAM_HDR_SIZE);
	if (!secondary_ssram) {
		ret = -ENOMEM;
		goto secondary_remap_fail;
	}

	pwrm_base = get_base(secondary_ssram, SSRAM_PWRM_OFFSET);
	devid = readw(secondary_ssram + SSRAM_DEVID_OFFSET);

	/* Find and register and PMC telemetry entries */
	pmc_add_pmt(pmcdev, ssram_base, main_ssram);

	map = pmc_core_find_regmap(pmcdev->regmap_list, devid);
	if (!map) {
		ret = -ENODEV;
		goto find_regmap_fail;
	}

	ret = pmc_core_pmc_add(pmcdev, pwrm_base, map, pmc_idx);

find_regmap_fail:
	iounmap(secondary_ssram);
secondary_remap_fail:
	iounmap(main_ssram);

	return ret;

}

static int
pmc_core_get_primary_pmc(struct pmc_dev *pmcdev)
{
	struct pci_dev *ssram_pcidev = pmcdev->ssram_pcidev;
	const struct pmc_reg_map *map;
	void __iomem *ssram;
	u64 ssram_base, pwrm_base;
	u16 devid;
	int ret;

	if (!pmcdev->regmap_list)
		return -ENOENT;

	/* The primary PMC (SOC die) BAR is BAR 0 in config space. */
	ssram_base = ssram_pcidev->resource[0].start;
	ssram = ioremap(ssram_base, SSRAM_HDR_SIZE);
	if (!ssram)
		return -ENOMEM;

	pwrm_base = get_base(ssram, SSRAM_PWRM_OFFSET);
	devid = readw(ssram + SSRAM_DEVID_OFFSET);

	/* Find and register and PMC telemetry entries */
	pmc_add_pmt(pmcdev, ssram_base, ssram);

	map = pmc_core_find_regmap(pmcdev->regmap_list, devid);
	if (!map) {
		ret = -ENODEV;
		goto find_regmap_fail;
	}

	ret = pmc_core_pmc_add(pmcdev, pwrm_base, map, PMC_IDX_MAIN);

find_regmap_fail:
	iounmap(ssram);

	return ret;
}

int pmc_core_ssram_init(struct pmc_dev *pmcdev)
{
	struct pci_dev *pcidev;
	int ret;

	pcidev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(20, 2));
	if (!pcidev)
		return -ENODEV;

	ret = pcim_enable_device(pcidev);
	if (ret)
		goto release_dev;

	pmcdev->ssram_pcidev = pcidev;

	ret = pmc_core_get_primary_pmc(pmcdev);
	if (ret)
		goto disable_dev;

	pmc_core_get_secondary_pmc(pmcdev, PMC_IDX_IOE, SSRAM_IOE_OFFSET);
	pmc_core_get_secondary_pmc(pmcdev, PMC_IDX_PCH, SSRAM_PCH_OFFSET);

	return 0;

disable_dev:
	pmcdev->ssram_pcidev = NULL;
	pci_disable_device(pcidev);
release_dev:
	pci_dev_put(pcidev);

	return ret;
}

MODULE_IMPORT_NS(INTEL_PMT);
MODULE_IMPORT_NS(INTEL_VSEC);
