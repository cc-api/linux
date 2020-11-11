/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_HFI_H
#define _ASM_X86_HFI_H

/* CPUID detection and enumeration definitions for HFI */

union hfi_capabilities {
	struct {
		u8	performance:1;
		u8	energy_efficiency:1;
		u8	__reserved:6;
	} split;
	u8 bits;
};

union cpuid6_edx {
	struct {
		union hfi_capabilities	capabilities;
		u32			table_pages:4;
		u32			__reserved:4;
		s32			index:16;
	} split;
	u32 full;
};

union cpuid6_ecx {
	struct {
		u32	dont_care0:8;
		u32	nr_classes:8;
		u32	dont_care1:16;
	} split;
	u32 full;
};

/**
 * struct hfi_hdr - Header of the HFI table
 * @perf_updated:	Hardware updated performance capabilities
 * @ee_updated:		Hardware updated energy efficiency capabilities
 *
 * Properties of the data in an HFI table. There exists one header per each
 * HFI class.
 */
struct hfi_hdr {
	u8	perf_updated;
	u8	ee_updated;
} __packed;

/**
 * struct hfi_table - Representation of an HFI table
 * @base_addr:		Base address of the local copy of the HFI table
 * @timestamp:		Timestamp of the last update of the local table.
 *			Located at the base of the local table.
 * @hdr:		Base address of the header of the local table
 * @data:		Base address of the data of the local table
 */
struct hfi_table {
	union {
		void			*base_addr;
		u64			*timestamp;
	};
	void			*hdr;
	void			*data;
};

/**
 * struct hfi_features - Supported HFI features
 * @nr_classes:		Number of classes supported
 * @nr_table_pages:	Size of the HFI table in 4KB pages
 * @cpu_stride:		Stride size to locate the capability data of a logical
 *			processor within the table (i.e., row stride)
 * @class_stride:	Stride size to locate a class within the capability
 *			data of a logical processor or the HFI table header
 * @hdr_size:		Size of the table header
 *
 * Parameters and supported features that are common to all HFI instances
 */
struct hfi_features {
	unsigned int	nr_classes;
	size_t		nr_table_pages;
	unsigned int	cpu_stride;
	unsigned int	class_stride;
	unsigned int	hdr_size;
};

#endif /* _ASM_X86_HFI_H */
