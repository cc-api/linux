/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * intel_tpmi.h: Intel TPMI core external interface
 */

#ifndef _INTEL_TPMI_H_
#define _INTEL_TPMI_H_

/*
 * List of supported TMPI IDs.
 * Some TMPI IDs are not used by Linux, so the numbers are not consecutive.
 */
enum intel_tpmi_id {
	TPMI_ID_RAPL = 0, /* Running Average Power Limit */
	TPMI_ID_PEM = 1, /* Power and Perf excursion Monitor */
	TPMI_ID_UNCORE = 2, /* Uncore Frequency Scaling */
	TPMI_ID_SST = 5, /* Speed Select Technology */
	TPMI_CONTROL_ID = 0x80, /* Special ID for getting feature status */
	TPMI_INFO_ID = 0x81, /* Special ID for PCI BDF and Package ID information */
};

/**
 * struct intel_tpmi_plat_info - Platform information for a TPMI device instance
 * @package_id:	CPU Package id
 * @bus_number:	PCI bus number
 * @device_number: PCI device number
 * @function_number: PCI function number
 *
 * Structure to store platform data for a TPMI device instance. This
 * struct is used to return data via tpmi_get_platform_data().
 */
struct intel_tpmi_plat_info {
	u8 package_id;
	u8 bus_number;
	u8 device_number;
	u8 function_number;
};

struct intel_tpmi_plat_info *tpmi_get_platform_data(struct auxiliary_device *auxdev);
struct resource *tpmi_get_resource_at_index(struct auxiliary_device *auxdev, int index);
int tpmi_get_resource_count(struct auxiliary_device *auxdev);
int intel_tpmi_readq(struct auxiliary_device *auxdev, const void __iomem *addr, u64 *val);
int intel_tpmi_writeq(struct auxiliary_device *auxdev, u64 value, void __iomem *addr);
int tpmi_get_feature_status(struct auxiliary_device *auxdev, int feature_id, int *read_blocked,
			    int *write_blocked);

/* In kernel interface only */
int tpmi_get_info(int package_id, int tpmi_id, int *num_entries, int *entry_size);
void __iomem *tpmi_get_mem(int package_id, int tpmi_id, int *size);
void tpmi_free_mem(void __iomem *mem);
#endif
