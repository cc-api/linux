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

#endif
