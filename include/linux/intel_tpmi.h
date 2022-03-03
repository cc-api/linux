// SPDX-License-Identifier: GPL-2.0
/*
 * Intel TPMI MFD MFD Driver interface
 */

#ifndef _INTEL_TPMI_H_
#define _INTEL_TPMI_H_

struct intel_tpmi_plat_info {
        int package_id;
        int bus_number;
        int device_number;
        int function_number;
};

struct intel_tpmi_plat_info *tpmi_get_platform_data(struct auxiliary_device *auxdev);
struct resource *tpmi_get_resource_at_index(struct auxiliary_device *auxdev, int index);
int tpmi_get_resource_count(struct auxiliary_device *auxdev);

/* In kernel interface only */
int tpmi_get_info(int package_id, int tpmi_id, int *num_entries, int *entry_size);
void __iomem *tpmi_get_mem(int package_id, int tpmi_id, int *size);
void tpmi_free_mem(void __iomem *mem);

#endif
