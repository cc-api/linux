/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_HFI_H
#define _ASM_X86_HFI_H

#include <linux/cpumask.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

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

/**
 * struct hfi_instance - Representation of an HFI instance (i.e., a table)
 * @local_table:	Local copy of HFI table for this instance
 * @cpus:		CPUs represented in this HFI table instance
 * @hw_table:		Pointer to the HFI table of this instance
 * @update_work:	Delayed work to process HFI updates
 * @notifier_chain:	Notification chain dedicated to this instance
 * @table_lock:		Lock to protect acceses to the table of this instance
 * @event_lock:		Lock to process HFI interrupts
 *
 * A set of parameters to parse and navigate a specific HFI table.
 */
struct hfi_instance {
	struct hfi_table		local_table;
	cpumask_var_t			cpus;
	void				*hw_table;
	struct delayed_work		update_work;
	struct blocking_notifier_head	notifier_chain;
	raw_spinlock_t			table_lock;
	raw_spinlock_t			event_lock;
#ifdef CONFIG_DEBUG_FS
	struct hfi_hdr			*cap_upd_hist;
	unsigned int			cap_upd_hist_idx;
#endif
};

/**
 * struct itd_ipcc - IPC class fields as used by Intel Thread Director
 * @split.class:	The IPC class used for scheduling after filtering
 *			hardware classification.
 * @split.class_tmp:	Classification as read from hardware.
 * @split.counter:	A counter to filter out temporary classifications.
 * @full:		Full IPC class as carried in a task_struct.
 */
union itd_ipcc {
	struct {
		unsigned int	class:9;
		unsigned int	class_tmp:9;
		unsigned int	counter:14;
	} split __packed;
	unsigned int full;
};

#if defined(CONFIG_INTEL_HFI_THERMAL)
bool intel_hfi_enabled(void);
int intel_hfi_max_instances(void);
int intel_hfi_build_virt_features(struct hfi_features *features, unsigned int nr_classes);
int intel_hfi_build_virt_table(struct hfi_table *table, struct hfi_features *features,
			       unsigned int nr_classes, unsigned int hfi_index,
			       unsigned int cpu);
struct hfi_instance *intel_hfi_instance(unsigned int cpu);
int intel_hfi_notifier_register(struct notifier_block *notifier,
				struct hfi_instance *hfi_instance);
int intel_hfi_notifier_unregister(struct notifier_block *notifier,
				  struct hfi_instance *hfi_instance);
#else
static inline bool intel_hfi_enabled(void) { return false; }
static inline int intel_hfi_max_instances(void) { return 0; }
static inline int intel_hfi_build_virt_features(struct hfi_features *features,
						unsigned int nr_classes) { return 0; }
static inline int intel_hfi_build_virt_table(struct hfi_table *table,
					     struct hfi_features *features,
					     unsigned int nr_classes, unsigned int hfi_index,
					     unsigned int cpu) { return 0; }
static inline struct hfi_instance *intel_hfi_instance(unsigned int cpu) { return NULL; }
static inline int intel_hfi_notifier_register(struct notifier_block *notifier,
					      struct hfi_instance *hfi_instance)
					      { return -ENODEV; }
static inline int intel_hfi_notifier_unregister(struct notifier_block *notifier,
						struct hfi_instance *hfi_instance)
						{ return -ENODEV; }
#endif

#endif /* _ASM_X86_HFI_H */
