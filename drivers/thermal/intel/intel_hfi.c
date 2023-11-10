// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hardware Feedback Interface Driver
 *
 * Copyright (c) 2021, Intel Corporation.
 *
 * Authors: Aubrey Li <aubrey.li@linux.intel.com>
 *          Ricardo Neri <ricardo.neri-calderon@linux.intel.com>
 *
 *
 * The Hardware Feedback Interface provides a performance and energy efficiency
 * capability information for each CPU in the system. Depending on the processor
 * model, hardware may periodically update these capabilities as a result of
 * changes in the operating conditions (e.g., power limits or thermal
 * constraints). On other processor models, there is a single HFI update
 * at boot.
 *
 * This file provides functionality to process HFI updates and relay these
 * updates to userspace.
 */

#define pr_fmt(fmt)  "intel-hfi: " fmt

#include <linux/bitops.h>
#include <linux/cpu.h>
#include <linux/cpufeature.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/math.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/percpu.h>
#include <linux/percpu-defs.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/processor.h>
#include <linux/sched/topology.h>
#include <linux/seqlock.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/suspend.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/topology.h>
#include <linux/workqueue.h>

#include <asm/intel-family.h>
#include <asm/msr.h>

#ifdef CONFIG_DEBUG_FS
#include <linux/cacheinfo.h>
#include <linux/debugfs.h>

#include <asm/cpu.h>
#endif

#include "intel_hfi.h"
#include "thermal_interrupt.h"

#include "../thermal_netlink.h"


/* Hardware Feedback Interface MSR configuration bits */
#define HW_FEEDBACK_PTR_VALID_BIT		BIT(0)
#define HW_FEEDBACK_CONFIG_HFI_ENABLE_BIT	BIT(0)
#define HW_FEEDBACK_CONFIG_ITD_ENABLE_BIT	BIT(1)
#define HW_FEEDBACK_THREAD_CONFIG_ENABLE_BIT	BIT(0)

/* CPUID detection and enumeration definitions for HFI */

#define CPUID_HFI_LEAF 6

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

union hfi_thread_feedback_char_msr {
	struct {
		u64	classid : 8;
		u64	__reserved : 55;
		u64	valid : 1;
	} split;
	u64 full;
};

/**
 * struct hfi_cpu_data - HFI capabilities per CPU
 * @perf_cap:		Performance capability
 * @ee_cap:		Energy efficiency capability
 *
 * Capabilities of a logical processor in the HFI table. These capabilities are
 * unitless and specific to each HFI class.
 */
struct hfi_cpu_data {
	u8	perf_cap;
	u8	ee_cap;
} __packed;

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
 * struct hfi_instance - Representation of an HFI instance (i.e., a table)
 * @local_table:	Base of the local copy of the HFI table
 * @timestamp:		Timestamp of the last update of the local table.
 *			Located at the base of the local table.
 * @hdr:		Base address of the header of the local table
 * @data:		Base address of the data of the local table
 * @cpus:		CPUs represented in this HFI table instance
 * @hw_table:		Pointer to the HFI table of this instance
 * @update_work:	Delayed work to process HFI updates
 * @table_lock:		Lock to protect acceses to the table of this instance
 * @event_lock:		Lock to process HFI interrupts
 *
 * A set of parameters to parse and navigate a specific HFI table.
 */
struct hfi_instance {
	union {
		void			*local_table;
		u64			*timestamp;
	};
	void			*hdr;
	void			*data;
	cpumask_var_t		cpus;
	void			*hw_table;
	struct delayed_work	update_work;
	raw_spinlock_t		table_lock;
	raw_spinlock_t		event_lock;
#ifdef CONFIG_DEBUG_FS
	struct hfi_hdr		*cap_upd_hist;
	unsigned int		cap_upd_hist_idx;
#endif
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
 * struct hfi_cpu_info - Per-CPU attributes to consume HFI data
 * @index:		Row of this CPU in its HFI table
 * @hfi_instance:	Attributes of the HFI table to which this CPU belongs
 *
 * Parameters to link a logical processor to an HFI table and a row within it.
 */
struct hfi_cpu_info {
	s16			index;
	struct hfi_instance	*hfi_instance;
#ifdef CONFIG_DEBUG_FS
	u8			type;
#endif
};

static DEFINE_PER_CPU(struct hfi_cpu_info, hfi_cpu_info) = { .index = -1 };

static int max_hfi_instances;
static struct hfi_instance *hfi_instances;

static struct hfi_features hfi_features;
static DEFINE_MUTEX(hfi_instance_lock);

static struct workqueue_struct *hfi_updates_wq;
#define HFI_UPDATE_INTERVAL		HZ
#define HFI_MAX_THERM_NOTIFY_COUNT	16

#ifdef CONFIG_DEBUG_FS
/* Received package-level interrupts that are not HFI events. */
static DEFINE_PER_CPU(u64, hfi_intr_not_hfi);
/* Received package-level interrupts when per-CPU data is not initialized. */
static DEFINE_PER_CPU(u64, hfi_intr_not_initialized);
/* Received package-level interrupts that are HFI events. */
static DEFINE_PER_CPU(u64, hfi_intr_received);
/* HFI events for which new delayed work was scheduled */
static DEFINE_PER_CPU(u64, hfi_intr_processed);
/* HFI events which delayed work was scheduled while there was previous work pending. */
static DEFINE_PER_CPU(u64, hfi_intr_skipped);
/* HFI events during which the event_lock was held by another CPU. */
static DEFINE_PER_CPU(u64, hfi_intr_ignored);
/* HFI events that did not have a newer timestamp */
static DEFINE_PER_CPU(u64, hfi_intr_bad_ts);

static u64 hfi_updates, hfi_updates_recovered;

#define HFI_CAP_UPD_HIST_SZ 2048

static bool alloc_hfi_cap_upd_hist(struct hfi_instance *hfi_instance)
{
	hfi_instance->cap_upd_hist = kzalloc(hfi_features.nr_classes *
					     sizeof(*hfi_instance->cap_upd_hist) *
					     HFI_CAP_UPD_HIST_SZ,
					     GFP_KERNEL);

	return !!hfi_instance->cap_upd_hist;
}

unsigned long __percpu *hfi_ipcc_history;

static bool alloc_hfi_ipcc_history(void)
{
	int cpu;

	if (!cpu_feature_enabled(X86_FEATURE_ITD))
		return false;

	/*
	 * Alloc memory for the number of supported classes plus
	 * unclassified.
	 */
	hfi_ipcc_history = __alloc_percpu(sizeof(*hfi_ipcc_history) *
					  hfi_features.nr_classes + 1,
					  sizeof(*hfi_ipcc_history));

	if (!hfi_ipcc_history)
		return NULL;

	/* Not clear that __alloc_percpu() initializes memory to 0. */
	for_each_possible_cpu(cpu) {
		unsigned long *history = per_cpu_ptr(hfi_ipcc_history, cpu);

		memset(history, 0, (hfi_features.nr_classes + 1) *
		       sizeof(*hfi_ipcc_history));
	}

	return !!hfi_ipcc_history;
}

static ssize_t hfi_ipcc_history_write(struct file *file, const char __user *ptr,
				      size_t len, loff_t *off)
{
	int cpu;

	if (!hfi_ipcc_history)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		unsigned long *history = per_cpu_ptr(hfi_ipcc_history, cpu);

		memset(history, 0, (hfi_features.nr_classes + 1) *
		       sizeof(*hfi_ipcc_history));
	}

	return len;
}

static int hfi_ipcc_history_show(struct seq_file *s, void *unused)
{
	int cpu, i;

	if (!hfi_ipcc_history)
		return -ENOMEM;

	seq_puts(s, "CPU\tUnclass\t");
	for (i = IPC_CLASS_UNCLASSIFIED; i < hfi_features.nr_classes; i++)
		seq_printf(s, "IPCC%d\t", i + 1);
	seq_puts(s, "\n");

	for_each_online_cpu(cpu) {
		unsigned long *history = per_cpu_ptr(hfi_ipcc_history, cpu);

		seq_printf(s, "%d\t", cpu);

		for (i = 0; i < hfi_features.nr_classes + 1; i++)
			seq_printf(s, "%lu\t", history[i]);

		seq_puts(s,"\n");
	}

	return 0;
}

static int hfi_ipcc_history_open(struct inode *inode, struct file *file)
{
	return single_open(file, hfi_ipcc_history_show, inode->i_private);
}

static const struct file_operations hfi_ipcc_history_fops = {
	.owner = THIS_MODULE,
	.open = hfi_ipcc_history_open,
	.read = seq_read,
	.write = hfi_ipcc_history_write,
	.llseek = seq_lseek,
	.release = single_release
};

static int hfi_features_show(struct seq_file *s, void *unused)
{
	union cpuid6_edx edx;

	edx.full = cpuid_edx(CPUID_HFI_LEAF);

	seq_printf(s, "ITD supported(CPUID)\t%u\n", boot_cpu_has(X86_FEATURE_ITD));
	seq_printf(s, "IPC classes supported(Kconfig)\t%u\n",
		   IS_ENABLED(CONFIG_IPC_CLASSES));
	seq_printf(s, "HRESET supported\t%u\n", boot_cpu_has(X86_FEATURE_HRESET));
	if (boot_cpu_has(X86_FEATURE_HRESET))
		seq_printf(s, "HRESET features\t0x%x\n", cpuid_ebx(0x20));
	seq_printf(s, "Number of classes\t%u\n", hfi_features.nr_classes);
	seq_printf(s, "Capabilities\tPerf:0x%x\tEEff:0x%x\tReserved:0x%x\n",
		   edx.split.capabilities.split.performance,
		   edx.split.capabilities.split.energy_efficiency,
		   edx.split.capabilities.split.__reserved);
	seq_printf(s, "Table pages\t%zu\n", hfi_features.nr_table_pages);
	seq_printf(s, "CPU stride\t0x%x\n", hfi_features.cpu_stride);
	seq_printf(s, "Class class stride\t0x%x\n", hfi_features.class_stride);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hfi_features);

static int hfi_class_score_show(struct seq_file *s, void *unused)
{
	struct hfi_instance *hfi_instance = s->private;
	int cpu, j;

	if (!cpu_feature_enabled(X86_FEATURE_ITD)) {
		seq_puts(s, "IPC classes not supported.\n");
		return 0;
	}

	if (!cpumask_weight(hfi_instance->cpus)) {
		seq_puts(s, "All CPUs offline\n");
		return 0;
	}

	seq_puts(s, "CPU\tUnclass\t");
	/* See comment below on valid class numbers. */
	for (j = IPC_CLASS_UNCLASSIFIED; j < hfi_features.nr_classes; j++)
		seq_printf(s, "IPCC%d\t", j + 1);
	seq_puts(s, "\n");

	for_each_cpu(cpu, hfi_instance->cpus) {
		seq_printf(s, "%4d", cpu);
		/*
		 * IPCC classes have a range of [1, hfi_features.nr_classes + 1].
		 * HFI classes have a range of [0,  hfi_features.nr_classes].
		 *
		 * Start the loop in 0 (IPC_CLASS_UNCLASSIFIED) to also dump the
		 * score used for unclassified tasks.
		 */
		for (j = IPC_CLASS_UNCLASSIFIED; j <= hfi_features.nr_classes; j++)
			seq_printf(s, "\t%3lu",
				   intel_hfi_get_ipcc_score(j, cpu));

		seq_puts(s, "\n");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hfi_class_score);

static int hfi_cap_upd_hist_show(struct seq_file *s, void *unused)
{
	struct hfi_instance *hfi_instance = s->private;
	int i, j;

	if (!hfi_instance->cap_upd_hist)
		return -ENOMEM;

	for (i = 0; i <  hfi_features.nr_classes; i++)
		seq_printf(s, "Pe%d\tEf%d\t", i, i);

	seq_puts(s, "\n");

	for (i = 0; i < (hfi_instance->cap_upd_hist_idx % HFI_CAP_UPD_HIST_SZ) ; i++) {
		struct hfi_hdr *hdr = hfi_instance->cap_upd_hist + i * hfi_features.nr_classes;

		for (j = 0; j < hfi_features.nr_classes; j++) {
			seq_printf(s, "0x%x\t0x%x\t", hdr->perf_updated, hdr->ee_updated);
			hdr++;
		}

		seq_puts(s, "\n");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hfi_cap_upd_hist);

/* See definition of CPUID.1A.EAX */
#define CPU_TYPE_CORE 0x40
#define CPU_TYPE_ATOM 0x20

static char get_cpu_type(int cpu)
{
	u8 type =  per_cpu(hfi_cpu_info, cpu).type;

	if (type == CPU_TYPE_CORE)
		return 'P';

	if (type == CPU_TYPE_ATOM) {
		switch (boot_cpu_data.x86_model) {
		case INTEL_FAM6_METEORLAKE:
		case INTEL_FAM6_METEORLAKE_L:
			if (get_cpu_cacheinfo(cpu)->num_leaves == 4)
				return 'E';
			if (get_cpu_cacheinfo(cpu)->num_leaves == 3)
				return 'L';

			return '?';
		default:
			return 'E';
		}
	}

	return '?';
}

static int hfi_state_show(struct seq_file *s, void *unused)
{
	struct hfi_instance *hfi_instance = s->private, hfi_tmp;
	struct hfi_hdr *hfi_hdr;
	int cpu, i, j, ret = 0;
	void *table_copy;
	u64 msr_val;

	mutex_lock(&hfi_instance_lock);

	cpu = cpumask_first(hfi_instance->cpus);

	if (cpu >= nr_cpu_ids) {
		seq_printf(s, "All CPUs offline\n");
		goto unlock;
	}

	table_copy = kzalloc(hfi_features.nr_table_pages << PAGE_SHIFT,
					    GFP_KERNEL);
	if (!table_copy) {
		ret = -ENOMEM;
		goto unlock;
	}

	/* Dump the relevant registers */
	rdmsrl_on_cpu(cpu, MSR_IA32_PACKAGE_THERM_STATUS, &msr_val);
	seq_printf(s, "MSR_IA32_PACKAGE_THERM_STATUS\t0x%llx\n", msr_val);
	seq_printf(s, "HFI status bit\t%lld\n", (msr_val & 0x4000000) >> 26);

	rdmsrl_on_cpu(cpu, MSR_IA32_PACKAGE_THERM_INTERRUPT, &msr_val);
	seq_printf(s, "MSR_IA32_PACKAGE_THERM_INTERRUPT\t0x%llx\n", msr_val);
	seq_printf(s, "HFI intr bit\t%lld\n", (msr_val & 0x2000000) >> 25);

	rdmsrl_on_cpu(cpu, MSR_IA32_HW_FEEDBACK_PTR, &msr_val);
	seq_printf(s, "MSR_IA32_HW_FEEDBACK_PTR\t0x%llx\n", msr_val);

	rdmsrl_on_cpu(cpu, MSR_IA32_HW_FEEDBACK_CONFIG, &msr_val);
	seq_printf(s, "MSR_IA32_HW_FEEDBACK_CONFIG\t0x%llx\n", msr_val);
	if (boot_cpu_has(X86_FEATURE_ITD)) {
		seq_puts(s, "\nCPU\tMSR_IA32_HW_HRESET_ENABLE\tMSR_IA32_HW_FEEDBACK_THREAD_CONFIG\n");
		for_each_cpu(i, hfi_instance->cpus) {
			u64 hreset_en, thr_cfg;

			rdmsrl_on_cpu(i, MSR_IA32_HW_HRESET_ENABLE, &hreset_en);
			rdmsrl_on_cpu(i, MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, &thr_cfg);
			seq_printf(s, "%4d\t0x%llx\t0x%llx\n", i, hreset_en, thr_cfg);
		}
		seq_puts(s, "\n");
	}

	raw_spin_lock_irq(&hfi_instance->table_lock);
	memcpy(table_copy, hfi_instance->local_table,
	       hfi_features.nr_table_pages << PAGE_SHIFT);
	raw_spin_unlock_irq(&hfi_instance->table_lock);

	hfi_tmp.local_table = table_copy;
	hfi_tmp.hdr = hfi_tmp.local_table + sizeof(*hfi_tmp.timestamp);
	hfi_tmp.data = hfi_tmp.hdr + hfi_features.hdr_size;

	/* Dump the HFI table parameters */
	seq_printf(s, "Table base\t0x%px\n", hfi_instance->local_table);
	seq_printf(s, "Headers base\t0x%px\n", hfi_instance->hdr);
	seq_printf(s, "Data base\t0x%px\n", hfi_instance->data);
	seq_printf(s, "Die id\t%u\n",
		   topology_logical_die_id(cpumask_first(hfi_instance->cpus)));
	seq_printf(s, "CPUs\t%*pbl\n", cpumask_pr_args(hfi_instance->cpus));
	/* Use our local temp copy. */
	seq_printf(s, "Timestamp\t%lld\n", *hfi_tmp.timestamp);
	seq_puts(s, "\nPer-CPU data\n");
	seq_puts(s, "CPU\tInstance data address:\tHFI interrupts\n");
	seq_puts(s, "\t\treceived\tnot hfi\tnot initialized\tprocessed\tskipped\tignored\tbad timestamp\n");
	for_each_cpu(i, hfi_instance->cpus) {
		seq_printf(s, "%4d\t%px", i, per_cpu(hfi_cpu_info, i).hfi_instance);
		seq_printf(s, "\t%6llu\t%6llu\t%6llu\t%6llu\t%6llu\t%6llu\t%6llu\n",
			   per_cpu(hfi_intr_received, i),
			   per_cpu(hfi_intr_not_hfi, i),
			   per_cpu(hfi_intr_not_initialized, i),
			   per_cpu(hfi_intr_processed, i),
			   per_cpu(hfi_intr_skipped, i),
			   per_cpu(hfi_intr_ignored, i),
			   per_cpu(hfi_intr_bad_ts, i));
	}

	/* Dump the performance capability change indication */
	seq_puts(s, "\nPerf Cap Change Indication\n");
	for (i = 0; i < hfi_features.nr_classes; i++)
		seq_printf(s, "C%d\t", i);

	seq_puts(s, "\n");

	hfi_hdr = hfi_tmp.hdr;
	for (i = 0; i < hfi_features.nr_classes; i++) {
		seq_printf(s, "0x%x\t", hfi_hdr->perf_updated);
		hfi_hdr++;
	}

	/* Dump the energy efficiency capability change indication */
	seq_puts(s, "\n\nEnergy Efficiency Cap Change Indication\n");
	for (i = 0; i < hfi_features.nr_classes; i++)
		seq_printf(s, "C%d\t", i);

	seq_puts(s, "\n");

	hfi_hdr = hfi_tmp.hdr;
	for (i = 0; i < hfi_features.nr_classes; i++) {
		seq_printf(s, "0x%x\t", hfi_hdr->ee_updated);
		hfi_hdr++;
	}

	/* Overall HFI updates in the system */
	seq_puts(s, "\n\nHFI table updates:\n");
	seq_printf(s, "scheduled\t%llu\nrecovered\t%llu\n",
		   hfi_updates, hfi_updates_recovered);

	/* Dump the HFI table */
	seq_puts(s, "\nHFI table\n");
	seq_puts(s, "CPU\tIndex\tType");
	for (i = 0; i < hfi_features.nr_classes; i++)
		seq_printf(s, "\tPe%u\tEf%u", i, i);
	seq_puts(s, "\n");

	for_each_cpu(i, hfi_instance->cpus) {
		s16 index = per_cpu(hfi_cpu_info, i).index;

		/* Use our local copy. */
		void *data_ptr = hfi_tmp.data +
				       index * hfi_features.cpu_stride;

		seq_printf(s, "%4u\t%4d\t%2c", i, index, get_cpu_type(i));
		for (j = 0; j < hfi_features.nr_classes; j++) {
			struct hfi_cpu_data *data = data_ptr +
						    j * hfi_features.class_stride;

			seq_printf(s, "\t%3u\t%3u", data->perf_cap, data->ee_cap);
		}

		seq_puts(s, "\n");
	}

	seq_puts(s, "\nIPCC scores:\n");
	hfi_class_score_show(s, NULL);

	kfree(table_copy);

unlock:
	mutex_unlock(&hfi_instance_lock);
	return ret;
}
DEFINE_SHOW_ATTRIBUTE(hfi_state);

/*
 * Inject an HFI table:
 *
 * The file /sys/kernel/debug/intel_hw_feedback/fake_tableX provides
 * functionality to inject an HFI table to an HFI instance. It can accept up to
 * 128 numeric entries in the format n,n,n,...,n,n where n are numbers in the
 * range [0, 255].
 *
 * You need to inject the values sequentially per capability, per class, per
 * row in the HFI table. For instance, if your hardware supports 4 classes, and
 * performance and energy efficiency, inject the values for the first row of
 * the HFI table as follows:
 *
 *	Pe0,Ee0,Pe1,Ee1,Pe2,Ee2,Pe3,Ee3,

 * Then, append the subsequent rows of the table after the comma (no spaces)
 * until you have as many rows as you need in the table.

 * You can optionally only provide a few rows of the table. In such case, the
 * injection functionality will use the provided values preriodically to
 * populate the whole injected table.
 *
 * When composing your table, remember that more than one CPU can point to the
 * same row in the table.
 */
#define HFI_FAKE_TABLE_MAX_ENTRIES 128
static char hfi_fake_table_input_str[HFI_FAKE_TABLE_MAX_ENTRIES * 4];
static u8 hfi_fake_table_input_vals[HFI_FAKE_TABLE_MAX_ENTRIES];

static int hfi_inject_table(struct hfi_instance *hfi_instance,
			    u8 *fake_table_vals, int fake_table_len)
{
	void *fake_table, *fake_hdr, *fake_data;
	struct hfi_hdr *hfi_hdr;
	u64 *fake_timestamp;
	int i, k = 0;

	if (!hfi_instance)
		return -ENODEV;

	fake_table = kzalloc(hfi_features.nr_table_pages << PAGE_SHIFT,
			     GFP_KERNEL);
	if (!fake_table)
		return -ENOMEM;

	/* The timestamp is at the base of the HFI table. */
	fake_timestamp = (u64 *)fake_table;
	/* The HFI header is below the time-stamp. */
	fake_hdr = fake_table + sizeof(*fake_timestamp);
	/* The HFI data starts below the header. */
	fake_data = fake_hdr + hfi_features.hdr_size;

	/* Fake timestamp. */
	*fake_timestamp = *hfi_instance->timestamp + 1;

	/* Fake header. */
	hfi_hdr = fake_hdr;
	for (i = 0; i < hfi_features.nr_classes; i++) {
		hfi_hdr->perf_updated = 5;
		hfi_hdr->ee_updated = 5;
		hfi_hdr++;
	}

	/* Fake data. */
	for (i = 0; i < HFI_FAKE_TABLE_MAX_ENTRIES; i++) {
		void *data_ptr = fake_data + i * hfi_features.cpu_stride;
		int j;

		for (j = 0; j < hfi_features.nr_classes; j++) {
			struct hfi_cpu_data *data = data_ptr +
						    j * hfi_features.class_stride;

			/* Keep reusing the same fake_table_vals values until done. */
			data->perf_cap = fake_table_vals[k++ % fake_table_len];
			data->ee_cap = fake_table_vals[k++ % fake_table_len];
		}
	}

	memcpy(hfi_instance->local_table, fake_table,
	       hfi_features.nr_table_pages << PAGE_SHIFT);

	queue_delayed_work(hfi_updates_wq, &hfi_instance->update_work,
			   HFI_UPDATE_INTERVAL);

	kfree(fake_table);

	return 0;
}

static int hfi_fake_table_parse_values(char *str, u8 *values)
{
	char *key;
	int i = 0, ret;

	while ((key = strsep(&str, ",")) != NULL) {
		ret = kstrtou8(key, 10, &values[i]);
		if (ret)
			return ret;

		i++;

		if (i == HFI_FAKE_TABLE_MAX_ENTRIES)
			goto out;
	}

out:
	return i;
}

static ssize_t hfi_fake_table_write(struct file *file, const char __user *ptr,
				    size_t len, loff_t *off)
{
	struct hfi_instance *hfi_instance;
	int ret;

	hfi_instance = ((struct seq_file *)file->private_data)->private;

	if (*off != 0)
		return 0;

	if (len > sizeof(hfi_fake_table_input_str))
		return -E2BIG;

	memset(hfi_fake_table_input_str, 0, sizeof(hfi_fake_table_input_str));
	memset(hfi_fake_table_input_vals, 0, sizeof(hfi_fake_table_input_vals));

	ret = strncpy_from_user(hfi_fake_table_input_str, ptr, len);
	if (ret < 0)
		return ret;

	ret = hfi_fake_table_parse_values(hfi_fake_table_input_str,
					  hfi_fake_table_input_vals);
	if (ret < 0)
		return ret;

	ret = hfi_inject_table(hfi_instance, hfi_fake_table_input_vals, ret);

	return ret ? ret : len;
}

static int hfi_fake_table_show(struct seq_file *s, void *unused)
{
	int i;

	for (i = 0; i < HFI_FAKE_TABLE_MAX_ENTRIES - 1; i++)
		seq_printf(s, "%u,", hfi_fake_table_input_vals[i]);

	seq_printf(s, "%u\n",
		   hfi_fake_table_input_vals[HFI_FAKE_TABLE_MAX_ENTRIES - 1]);

	return 0;
}

static int hfi_fake_table_open(struct inode *inode, struct file *file)
{
	return single_open(file, hfi_fake_table_show, inode->i_private);
}

static const struct file_operations hfi_fake_table_fops = {
	.owner = THIS_MODULE,
	.open = hfi_fake_table_open,
	.read = seq_read,
	.write = hfi_fake_table_write,
	.llseek = seq_lseek,
	.release = single_release
};

static struct dentry *hfi_debugfs_dir;

#ifdef CONFIG_IPC_CLASSES
extern unsigned long itd_class_debouncer_skips;

static int itd_debouncer_skip_get(void *data, u64 *val)
{
	*val = itd_class_debouncer_skips;
	return 0;
}

static int itd_debouncer_skip_set(void *data, u64 val)
{
	itd_class_debouncer_skips = val;
	return 0;
}
#else
static int itd_debouncer_skip_get(void *data, u64 *val)
{
	return -EPERM;
}

static int itd_debouncer_skip_set(void *data, u64 val)
{
	return -EPERM;
}
#endif

DEFINE_DEBUGFS_ATTRIBUTE(itd_debouncer_skip_fops, itd_debouncer_skip_get,
			 itd_debouncer_skip_set, "%llu\n");

static void hfi_debugfs_unregister(void)
{
	struct hfi_instance *hfi;
	int i;

	debugfs_remove_recursive(hfi_debugfs_dir);
	hfi_debugfs_dir = NULL;

	free_percpu(hfi_ipcc_history);
	hfi_ipcc_history = NULL;

	for (i = 0; i < max_hfi_instances; i++) {
		hfi = &hfi_instances[i];
		kfree(hfi->cap_upd_hist);
		hfi->cap_upd_hist = NULL;
	}
}

static void hfi_debugfs_register(void)
{
	struct dentry *f;

	hfi_debugfs_dir = debugfs_create_dir("intel_hw_feedback", NULL);
	if (!hfi_debugfs_dir)
		return;

	f = debugfs_create_file("features", 0444, hfi_debugfs_dir,
				NULL, &hfi_features_fops);
	if (!f)
		goto err;

	f = debugfs_create_file("debounce_skips", 0444, hfi_debugfs_dir,
				NULL, &itd_debouncer_skip_fops);
	if (!f)
		goto err;

	if (!alloc_hfi_ipcc_history())
		goto err;

	f = debugfs_create_file("ipcc_history", 0444, hfi_debugfs_dir,
				NULL, &hfi_ipcc_history_fops);
	if (!f)
		goto err;

	return;

err:
	hfi_debugfs_unregister();
}

static void hfi_debugfs_populate_instance(struct hfi_instance *hfi_instance,
					  int die_id)
{
	struct dentry *f;
	char name[64];

	if (!hfi_debugfs_dir)
		return;

	snprintf(name, 64, "hw_state%u", die_id);
	f = debugfs_create_file(name, 0444, hfi_debugfs_dir,
				hfi_instance, &hfi_state_fops);
	if (!f)
		goto err;

	snprintf(name, 64, "class_score%d", die_id);
	f = debugfs_create_file(name, 0444, hfi_debugfs_dir,
				hfi_instance, &hfi_class_score_fops);
	if (!f)
		goto err;

	snprintf(name, 64, "fake_table%u", die_id);
	f = debugfs_create_file(name, 0444, hfi_debugfs_dir,
				hfi_instance, &hfi_fake_table_fops);
	if (!f)
		goto err;

	if (!alloc_hfi_cap_upd_hist(hfi_instance))
		goto err;

	snprintf(name, 64, "cap_update_history%u", die_id);
	f = debugfs_create_file(name, 0444, hfi_debugfs_dir,
				hfi_instance, &hfi_cap_upd_hist_fops);
	if (!f)
		goto err;

	return;

err:
	hfi_debugfs_unregister();
}

#ifdef CONFIG_PROC_FS
static int hfi_proc_classid_show(struct seq_file *m, void *v)
{
	union hfi_thread_feedback_char_msr msr;
	unsigned long flags;

	if (!cpu_feature_enabled(X86_FEATURE_ITD))
		seq_printf(m, "%d\n", -ENODEV);

	get_cpu();
	local_irq_save(flags);

	rdmsrl(MSR_IA32_HW_FEEDBACK_CHAR, msr.full);

	if (!msr.split.valid) {
		seq_printf(m, "%d\n", IPC_CLASS_UNCLASSIFIED);
		goto out;
	}

	seq_printf(m, "%d\n", msr.split.classid + 1);

out:
	local_irq_restore(flags);
	put_cpu();

	return 0;
}

static int hfi_proc_classid_open(struct inode *inode, struct file *file)
{
	return single_open(file, hfi_proc_classid_show, NULL);
}

static const struct proc_ops get_hw_classid_ops = {
	.proc_open	= hfi_proc_classid_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static __init int proc_fs_register(void)
{
	struct proc_dir_entry *entry;

	entry = proc_create("classid", 0, NULL, &get_hw_classid_ops);
	if (!entry)
		pr_err("Unable to create /proc/classid!\n");

	return entry ? 0 : -ENODEV;
}
#else
static __init int proc_fs_register(void) { return 0; }
#endif

#else
static void hfi_debugfs_register(void)
{
}

static void hfi_debugfs_populate_instance(struct hfi_instance *hfi_instance,
					  int die_id)
{
}
#endif /* CONFIG_DEBUG_FS */

/**
 * enum hfi_user_config - Enablement states as provided by the user
 * @HFI_USER_CFG_DEFAULT:	User does not configure the HFI in the kernel
 * 				command line. HFI is enabled if hardware
 * 				supports it and is not broken.
 * @HFI_USER_CFG_DISABLE:	User disables the HFI in the kernel command
 * 				line.
 * @HFI_USER_CFG_FORCE_ENABLE:	User force-enables the HFI. It will be enabled
 * 				if hardware supports it but is broken.
 */
enum hfi_user_config {
	HFI_USER_CFG_DEFAULT = 0,
	HFI_USER_CFG_DISABLE,
	HFI_USER_CFG_FORCE_ENABLE
};

static enum hfi_user_config hfi_user_config;

/*
 * A task may be unclassified if it has been recently created, spend most of
 * its lifetime sleeping, or hardware has not provided a classification.
 *
 * Most tasks will be classified as scheduler's IPC class 1 (HFI class 0)
 * eventually. Meanwhile, the scheduler will place classes of tasks with higher
 * IPC scores on higher-performance CPUs.
 *
 * IPC class 1 is a reasonable choice. It matches the performance capability
 * of the legacy, classless, HFI table.
 */
#define HFI_UNCLASSIFIED_DEFAULT 1

/* A cache of the HFI perf capabilities for lockless access. */
static int __percpu *hfi_ipcc_scores;
/* Sequence counter for hfi_ipcc_scores */
static seqcount_t hfi_ipcc_seqcount = SEQCNT_ZERO(hfi_ipcc_seqcount);

static bool alloc_hfi_ipcc_scores(void)
{
	/* IPC scores are not needed without support for ITD. Do not fail. */
	if (!cpu_feature_enabled(X86_FEATURE_ITD))
		return true;

	hfi_ipcc_scores = __alloc_percpu(sizeof(*hfi_ipcc_scores) *
					 hfi_features.nr_classes,
					 sizeof(*hfi_ipcc_scores));

	return !!hfi_ipcc_scores;
}

long intel_hfi_get_ipcc_score(unsigned int ipcc, int cpu)
{
	int *scores, score;
	unsigned long seq;

	scores = per_cpu_ptr(hfi_ipcc_scores, cpu);
	if (!scores)
		return -ENODEV;

	if (cpu < 0 || cpu >= nr_cpu_ids)
		return -EINVAL;

	if (ipcc == IPC_CLASS_UNCLASSIFIED)
		ipcc = HFI_UNCLASSIFIED_DEFAULT;

	/*
	 * Scheduler IPC classes start at 1. HFI classes start at 0.
	 * See note intel_hfi_update_ipcc().
	 */
	if (ipcc >= hfi_features.nr_classes + 1)
		return -EINVAL;

	/*
	 * The seqcount implies load-acquire semantics to order loads with
	 * lockless stores of the write side in set_hfi_ipcc_score(). It
	 * also implies a compiler barrier.
	 */
	do {
		seq = read_seqcount_begin(&hfi_ipcc_seqcount);
		/* @ipcc is never 0. */
		score = scores[ipcc - 1];
	} while (read_seqcount_retry(&hfi_ipcc_seqcount, seq));

	return score;
}

static void set_hfi_ipcc_scores(struct hfi_instance *hfi_instance)
{
	int cpu;

	if (!cpu_feature_enabled(X86_FEATURE_ITD))
		return;

	/*
	 * Serialize with writes to the HFI table. It also protects the write
	 * loop against seqcount readers running in interrupt context.
	 */
	raw_spin_lock_irq(&hfi_instance->table_lock);
	/*
	 * The seqcount implies store-release semantics to order stores with
	 * lockless loads from the seqcount read side in
	 * intel_hfi_get_ipcc_score(). It also implies a compiler barrier.
	 */
	write_seqcount_begin(&hfi_ipcc_seqcount);
	for_each_cpu(cpu, hfi_instance->cpus) {
		int c, *scores;
		s16 index;

		index = per_cpu(hfi_cpu_info, cpu).index;
		scores = per_cpu_ptr(hfi_ipcc_scores, cpu);

		for (c = 0;  c < hfi_features.nr_classes; c++) {
			struct hfi_cpu_data *caps;

			caps = hfi_instance->data +
			       index * hfi_features.cpu_stride +
			       c * hfi_features.class_stride;
			scores[c] = caps->perf_cap;
		}
	}

	write_seqcount_end(&hfi_ipcc_seqcount);
	raw_spin_unlock_irq(&hfi_instance->table_lock);
}

/**
 * intel_hfi_read_classid() - Read the currrent classid
 * @classid:	Variable to which the classid will be written.
 *
 * Read the classification that Intel Thread Director has produced when this
 * function is called. Thread classification must be enabled before calling
 * this function.
 *
 * Return: 0 if the produced classification is valid. Error otherwise.
 */
int intel_hfi_read_classid(u8 *classid)
{
	union hfi_thread_feedback_char_msr msr;

	/* We should not be here if ITD is not supported. */
	if (!cpu_feature_enabled(X86_FEATURE_ITD)) {
		pr_warn_once("task classification requested but not supported!");
		return -ENODEV;
	}

	rdmsrl(MSR_IA32_HW_FEEDBACK_CHAR, msr.full);
	if (!msr.split.valid)
		return -EINVAL;

	*classid = msr.split.classid;
	return 0;
}

static void get_hfi_caps(struct hfi_instance *hfi_instance,
			 struct thermal_genl_cpu_caps *cpu_caps)
{
	int cpu, i = 0;

	raw_spin_lock_irq(&hfi_instance->table_lock);
	for_each_cpu(cpu, hfi_instance->cpus) {
		struct hfi_cpu_data *caps;
		s16 index;

		index = per_cpu(hfi_cpu_info, cpu).index;
		caps = hfi_instance->data + index * hfi_features.cpu_stride;
		cpu_caps[i].cpu = cpu;

		/*
		 * Scale performance and energy efficiency to
		 * the [0, 1023] interval that thermal netlink uses.
		 */
		cpu_caps[i].performance = caps->perf_cap << 2;
		cpu_caps[i].efficiency = caps->ee_cap << 2;

		++i;
	}
	raw_spin_unlock_irq(&hfi_instance->table_lock);
}

#define HFI_HEADER_BIT_FORCED_IDLE	BIT(1)

/*
 * Call update_capabilities() when there are changes in the HFI table.
 */
static void update_capabilities(struct hfi_instance *hfi_instance)
{
	struct thermal_genl_cpu_caps *cpu_caps;
	bool forced_idle = false;
	struct hfi_hdr *hfi_hdr;
	int i = 0, cpu_count;

	/* CPUs may come online/offline while processing an HFI update. */
	mutex_lock(&hfi_instance_lock);

	hfi_hdr = hfi_instance->hdr;
	if (hfi_hdr->perf_updated & HFI_HEADER_BIT_FORCED_IDLE)
		forced_idle = true;

	cpu_count = cpumask_weight(hfi_instance->cpus);

	/* No CPUs to report in this hfi_instance. */
	if (!cpu_count)
		goto out;

	cpu_caps = kcalloc(cpu_count, sizeof(*cpu_caps), GFP_KERNEL);
	if (!cpu_caps)
		goto out;

	get_hfi_caps(hfi_instance, cpu_caps);

	if (cpu_count < HFI_MAX_THERM_NOTIFY_COUNT)
		goto last_cmd;

	/* Process complete chunks of HFI_MAX_THERM_NOTIFY_COUNT capabilities. */
	for (i = 0;
	     (i + HFI_MAX_THERM_NOTIFY_COUNT) <= cpu_count;
	     i += HFI_MAX_THERM_NOTIFY_COUNT) {
		if (forced_idle)
			thermal_genl_cpu_forced_idle_event(HFI_MAX_THERM_NOTIFY_COUNT,
							   &cpu_caps[i]);
		else
			thermal_genl_cpu_capability_event(HFI_MAX_THERM_NOTIFY_COUNT,
							  &cpu_caps[i]);
	}

	cpu_count = cpu_count - i;

last_cmd:
	/* Process the remaining capabilities if any. */
	if (cpu_count) {
		if (forced_idle)
			thermal_genl_cpu_forced_idle_event(cpu_count, &cpu_caps[i]);
		else
			thermal_genl_cpu_capability_event(cpu_count, &cpu_caps[i]);
	}

	kfree(cpu_caps);

	set_hfi_ipcc_scores(hfi_instance);
out:
	mutex_unlock(&hfi_instance_lock);
}

static void hfi_update_work_fn(struct work_struct *work)
{
	struct hfi_instance *hfi_instance;

	hfi_instance = container_of(to_delayed_work(work), struct hfi_instance,
				    update_work);

#ifdef CONFIG_DEBUG_FS
	/*
	 * Here we are misusing hfi_instance_lock, which is meant to protect accesses to
	 * HFI instsances. It, however, needlessly protect accesses to all instances at the
	 * same time. We explot this to protect hfi_updtes. If in the future there is a per-
	 * instance lock, we would need to have our own lock.
	 */
	mutex_lock(&hfi_instance_lock);
	hfi_updates++;
	mutex_unlock(&hfi_instance_lock);
#endif

	update_capabilities(hfi_instance);
}

void intel_hfi_process_event(__u64 pkg_therm_status_msr_val)
{
	struct hfi_instance *hfi_instance;
	int cpu = smp_processor_id();
	struct hfi_cpu_info *info;
	u64 new_timestamp, msr, hfi;
#ifdef CONFIG_DEBUG_FS
	bool work_queued;

	per_cpu(hfi_intr_received, cpu)++;
#endif

	if (!pkg_therm_status_msr_val) {
#ifdef CONFIG_DEBUG_FS
		per_cpu(hfi_intr_not_hfi, cpu)++;
#endif
		return;
	}

	info = &per_cpu(hfi_cpu_info, cpu);
	if (!info) {
#ifdef CONFIG_DEBUG_FS
		per_cpu(hfi_intr_not_initialized, cpu)++;
#endif
		return;
	}

	/*
	 * A CPU is linked to its HFI instance before the thermal vector in the
	 * local APIC is unmasked. Hence, info->hfi_instance cannot be NULL
	 * when receiving an HFI event.
	 */
	hfi_instance = info->hfi_instance;
	if (unlikely(!hfi_instance)) {
		pr_debug("Received event on CPU %d but instance was null", cpu);
#ifdef CONFIG_DEBUG_FS
		per_cpu(hfi_intr_not_initialized, cpu)++;
#endif
		return;
	}

	/*
	 * On most systems, all CPUs in the package receive a package-level
	 * thermal interrupt when there is an HFI update. It is sufficient to
	 * let a single CPU to acknowledge the update and queue work to
	 * process it. The remaining CPUs can resume their work.
	 */
	if (!raw_spin_trylock(&hfi_instance->event_lock)) {
#ifdef CONFIG_DEBUG_FS
		per_cpu(hfi_intr_ignored, cpu)++;
#endif
		return;
	}

	rdmsrl(MSR_IA32_PACKAGE_THERM_STATUS, msr);
	hfi = msr & PACKAGE_THERM_STATUS_HFI_UPDATED;
	if (!hfi) {
		raw_spin_unlock(&hfi_instance->event_lock);
		return;
	}

	/*
	 * Ack duplicate update. Since there is an active HFI
	 * status from HW, it must be a new event, not a case
	 * where a lagging CPU entered the locked region.
	 */
	new_timestamp = *(u64 *)hfi_instance->hw_table;
	if (*hfi_instance->timestamp == new_timestamp) {

		thermal_clear_package_intr_status(PACKAGE_LEVEL, PACKAGE_THERM_STATUS_HFI_UPDATED);

#ifdef CONFIG_DEBUG_FS
		per_cpu(hfi_intr_bad_ts, cpu)++;
#endif

		raw_spin_unlock(&hfi_instance->event_lock);
		return;
	}

	raw_spin_lock(&hfi_instance->table_lock);

	/*
	 * Copy the updated table into our local copy. This includes the new
	 * timestamp.
	 */
	memcpy(hfi_instance->local_table, hfi_instance->hw_table,
	       hfi_features.nr_table_pages << PAGE_SHIFT);

#ifdef CONFIG_DEBUG_FS
	if (hfi_instance->cap_upd_hist) {
		memcpy(hfi_instance->cap_upd_hist + ((hfi_instance->cap_upd_hist_idx %
						      HFI_CAP_UPD_HIST_SZ) *
						     hfi_features.nr_classes),
		       /* Skip the timestamp */
		       hfi_instance->hw_table + sizeof(hfi_instance->timestamp),
		       hfi_features.nr_classes * sizeof(*hfi_instance->cap_upd_hist));

		hfi_instance->cap_upd_hist_idx++;
	}
#endif

	/*
	 * Let hardware know that we are done reading the HFI table and it is
	 * free to update it again.
	 */
	thermal_clear_package_intr_status(PACKAGE_LEVEL, PACKAGE_THERM_STATUS_HFI_UPDATED);

	raw_spin_unlock(&hfi_instance->table_lock);
	raw_spin_unlock(&hfi_instance->event_lock);

#ifdef CONFIG_DEBUG_FS
	work_queued = queue_delayed_work(hfi_updates_wq,
					 &hfi_instance->update_work,
					 HFI_UPDATE_INTERVAL);
	if (work_queued)
		per_cpu(hfi_intr_processed, cpu)++;
	else
		per_cpu(hfi_intr_skipped, cpu)++;
#else
	queue_delayed_work(hfi_updates_wq, &hfi_instance->update_work,
			   HFI_UPDATE_INTERVAL);
#endif
}

static void init_hfi_cpu_index(struct hfi_cpu_info *info)
{
	union cpuid6_edx edx;

	/* Do not re-read @cpu's index if it has already been initialized. */
	if (info->index > -1)
		return;

	edx.full = cpuid_edx(CPUID_HFI_LEAF);
	info->index = edx.split.index;
#ifdef CONFIG_DEBUG_FS
	info->type = get_this_hybrid_cpu_type();
#endif
}

/*
 * The format of the HFI table depends on the number of capabilities and classes
 * that the hardware supports. Keep a data structure to navigate the table.
 */
static void init_hfi_instance(struct hfi_instance *hfi_instance)
{
	/* The HFI header is below the time-stamp. */
	hfi_instance->hdr = hfi_instance->local_table +
			    sizeof(*hfi_instance->timestamp);

	/* The HFI data starts below the header. */
	hfi_instance->data = hfi_instance->hdr + hfi_features.hdr_size;
}

static ssize_t intel_hfi_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct hfi_cpu_info *info = &per_cpu(hfi_cpu_info, dev->id);

	return sysfs_emit(buf, "%u\n", !!info->hfi_instance);
}

static DEVICE_ATTR_RO(intel_hfi);

static void intel_hfi_add_state_sysfs(int cpu)
{
	struct device *dev = get_cpu_device(cpu);

	if (!dev)
		goto err;

	if (sysfs_create_file(&dev->kobj, &dev_attr_intel_hfi.attr))
		goto err;

	return;
err:
	pr_err("Failed to register state sysfs!");
}

static void intel_hfi_remove_state_sysfs(int cpu)
{
	struct device *dev = get_cpu_device(cpu);

	if (!dev)
		return;

	sysfs_remove_file(&dev->kobj, &dev_attr_intel_hfi.attr);
}

static void hfi_enable(void)
{
	u64 msr_val;

	rdmsrl(MSR_IA32_HW_FEEDBACK_CONFIG, msr_val);
	msr_val |= HW_FEEDBACK_CONFIG_HFI_ENABLE_BIT;

	if (cpu_feature_enabled(X86_FEATURE_ITD))
		msr_val |= HW_FEEDBACK_CONFIG_ITD_ENABLE_BIT;

	wrmsrl(MSR_IA32_HW_FEEDBACK_CONFIG, msr_val);
}

static void hfi_set_hw_table(struct hfi_instance *hfi_instance)
{
	u64 msr_val;
	phys_addr_t hw_table_pa;

	hw_table_pa = virt_to_phys(hfi_instance->hw_table);
	msr_val = hw_table_pa | HW_FEEDBACK_PTR_VALID_BIT;
	wrmsrl(MSR_IA32_HW_FEEDBACK_PTR, msr_val);
}

static void hfi_disable(void)
{
	u64 msr_val;

	rdmsrl(MSR_IA32_HW_FEEDBACK_CONFIG, msr_val);
	msr_val &= ~HW_FEEDBACK_CONFIG_HFI_ENABLE_BIT;

	if (cpu_feature_enabled(X86_FEATURE_ITD))
		msr_val &= ~HW_FEEDBACK_CONFIG_ITD_ENABLE_BIT;

	wrmsrl(MSR_IA32_HW_FEEDBACK_CONFIG, msr_val);
}

static void hfi_enable_itd_classification(void)
{
	u64 msr_val;

	if (!cpu_feature_enabled(X86_FEATURE_ITD))
		return;

	rdmsrl(MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, msr_val);
	msr_val |= HW_FEEDBACK_THREAD_CONFIG_ENABLE_BIT;
	wrmsrl(MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, msr_val);
}

static void hfi_disable_itd_classification(void)
{
	u64 msr_val;

	if (!cpu_feature_enabled(X86_FEATURE_ITD))
		return;

	rdmsrl(MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, msr_val);
	msr_val &= ~HW_FEEDBACK_THREAD_CONFIG_ENABLE_BIT;
	wrmsrl(MSR_IA32_HW_FEEDBACK_THREAD_CONFIG, msr_val);
}

/**
 * intel_hfi_online() - Enable HFI on @cpu
 * @cpu:	CPU in which the HFI will be enabled
 *
 * Enable the HFI to be used in @cpu. The HFI is enabled at the die/package
 * level. The first CPU in the die/package to come online does the full HFI
 * initialization. Subsequent CPUs will just link themselves to the HFI
 * instance of their die/package.
 *
 * This function is called before enabling the thermal vector in the local APIC
 * in order to ensure that @cpu has an associated HFI instance when it receives
 * an HFI event.
 */
void intel_hfi_online(unsigned int cpu)
{
	struct hfi_instance *hfi_instance;
	struct hfi_cpu_info *info;
	u16 die_id;

	intel_hfi_add_state_sysfs(cpu);

	/* Nothing to do if hfi_instances are missing. */
	if (!hfi_instances)
		return;

	/*
	 * Link @cpu to the HFI instance of its package/die. It does not
	 * matter whether the instance has been initialized.
	 */
	info = &per_cpu(hfi_cpu_info, cpu);
	die_id = topology_logical_die_id(cpu);
	hfi_instance = info->hfi_instance;
	if (!hfi_instance) {
		if (die_id >= max_hfi_instances)
			return;

		hfi_instance = &hfi_instances[die_id];
		info->hfi_instance = hfi_instance;
	}

	init_hfi_cpu_index(info);

	hfi_enable_itd_classification();

	/*
	 * Now check if the HFI instance of the package/die of @cpu has been
	 * initialized (by checking its header). In such case, all we have to
	 * do is to add @cpu to this instance's cpmuask and enable the instance
	 * if needed.
	 */
	mutex_lock(&hfi_instance_lock);
	if (hfi_instance->hdr) {
		u64 msr_val;

		/*
		 * Both the HFI thermal interrupt and the local APIC thermal LVT
		 * are enabled when a CPU comes online. On some systems, all
		 * CPUs get the package thermak interrupt. On others, however,
		 * only a subset of CPU gets it. In the former case, we always
		 * get the interrupt as we enable the HFI after having enabled
		 * the thermal interrupt in the local APIC. However, in the
		 * latter case, we may miss the interrupt if hardware issues the
		 * interrupt to a CPU in which the thermal vector has not been
		 * enabled in the local APIC. We know that this is the case as
		 * the status bit will be set. In such a case, handle the
		 * interrupt.
		 */
		raw_spin_lock_irq(&hfi_instance->table_lock);
		rdmsrl(MSR_IA32_PACKAGE_THERM_STATUS, msr_val);
		if (msr_val & PACKAGE_THERM_STATUS_HFI_UPDATED) {
			memcpy(hfi_instance->local_table, hfi_instance->hw_table,
			       hfi_features.nr_table_pages << PAGE_SHIFT);

			thermal_clear_package_intr_status(PACKAGE_LEVEL, PACKAGE_THERM_STATUS_HFI_UPDATED);

			raw_spin_unlock_irq(&hfi_instance->table_lock);

			queue_delayed_work(hfi_updates_wq,
					   &hfi_instance->update_work,
					   HFI_UPDATE_INTERVAL);
#ifdef CONFIG_DEBUG_FS
			hfi_updates_recovered++;
#endif

			goto enable;
		}

		raw_spin_unlock_irq(&hfi_instance->table_lock);

		goto enable;
	}

	/*
	 * Hardware is programmed with the physical address of the first page
	 * frame of the table. Hence, the allocated memory must be page-aligned.
	 *
	 * On some processors, hardware remembers the first address of the HFI
	 * table even after having been reprogrammed and re-enabled. Thus, do
	 * not free the pages allocated for the table or reprogram the hardware
	 * with a different base address.
	 */
	hfi_instance->hw_table = alloc_pages_exact(hfi_features.nr_table_pages,
						   GFP_KERNEL | __GFP_ZERO);
	if (!hfi_instance->hw_table)
		goto unlock;

	/*
	 * Allocate memory to keep a local copy of the table that
	 * hardware generates.
	 */
	hfi_instance->local_table = kzalloc(hfi_features.nr_table_pages << PAGE_SHIFT,
					    GFP_KERNEL);
	if (!hfi_instance->local_table)
		goto free_hw_table;

	init_hfi_instance(hfi_instance);

	INIT_DELAYED_WORK(&hfi_instance->update_work, hfi_update_work_fn);
	raw_spin_lock_init(&hfi_instance->table_lock);
	raw_spin_lock_init(&hfi_instance->event_lock);

enable:
	cpumask_set_cpu(cpu, hfi_instance->cpus);

	/* If this is the first CPU, enable HFI in this package/die. */
	if (cpumask_weight(hfi_instance->cpus) == 1) {
		hfi_set_hw_table(hfi_instance);
		hfi_enable();

		hfi_debugfs_populate_instance(hfi_instance, die_id);
	}

	/*
	 * We have all we need to support IPC classes. Task classification is
	 * now working.
	 *
	 * All class scores are zero until after the first HFI update. That is
	 * OK. The scheduler queries these scores at every load balance.
	 */
	if (cpu_feature_enabled(X86_FEATURE_ITD))
		sched_enable_ipc_classes();

unlock:
	mutex_unlock(&hfi_instance_lock);
	return;

free_hw_table:
	free_pages_exact(hfi_instance->hw_table, hfi_features.nr_table_pages);
	goto unlock;
}

/**
 * intel_hfi_offline() - Disable HFI on @cpu
 * @cpu:	CPU in which the HFI will be disabled
 *
 * Remove @cpu from those covered by its HFI instance.
 *
 * On some processors, hardware remembers previous programming settings even
 * after being reprogrammed. Thus, keep HFI enabled even if all CPUs in the
 * die/package of @cpu are offline. See note in intel_hfi_online().
 */
void intel_hfi_offline(unsigned int cpu)
{
	struct hfi_cpu_info *info = &per_cpu(hfi_cpu_info, cpu);
	struct hfi_instance *hfi_instance;

	intel_hfi_remove_state_sysfs(cpu);

	/*
	 * Check if @cpu as an associated, initialized (i.e., with a non-NULL
	 * header). Also, HFI instances are only initialized if X86_FEATURE_HFI
	 * is present.
	 */
	hfi_instance = info->hfi_instance;
	if (!hfi_instance)
		return;

	if (!hfi_instance->hdr)
		return;

	hfi_disable_itd_classification();

	mutex_lock(&hfi_instance_lock);
	cpumask_clear_cpu(cpu, hfi_instance->cpus);

	if (!cpumask_weight(hfi_instance->cpus))
		hfi_disable();

	mutex_unlock(&hfi_instance_lock);
}

static bool hfi_is_broken(void)
{
	switch(boot_cpu_data.x86_model) {
#if 0
	case INTEL_FAM6_METEORLAKE:
	case INTEL_FAM6_METEORLAKE_L:
		return true;
#endif
	default:
		return false;
	}
}

static __init int intel_hfi_parse_options(char *str)
{
	if (parse_option_str(str, "force_enable")) {
		if (!boot_cpu_has(X86_FEATURE_HFI)) {
			pr_err("Cannot force-enable HFI. Hardware does not support it!\n");
			return 1;
		}

		if (hfi_is_broken())
			pr_info("Force-enabling HFI in broken hardware");

		hfi_user_config = HFI_USER_CFG_FORCE_ENABLE;
		return 1;
	}

	if (parse_option_str(str, "disable"))
		hfi_user_config = HFI_USER_CFG_DISABLE;

	return 1;
}
__setup("intel_hfi=", intel_hfi_parse_options);

static __init int hfi_parse_features(void)
{
	unsigned int nr_capabilities;
	union cpuid6_edx edx;

	if (hfi_user_config == HFI_USER_CFG_DISABLE)
		return -EPERM;

	if (hfi_is_broken() && hfi_user_config != HFI_USER_CFG_FORCE_ENABLE)
		return -EPERM;

	if (!boot_cpu_has(X86_FEATURE_HFI))
		return -ENODEV;

	/*
	 * If we are here we know that CPUID_HFI_LEAF exists. Parse the
	 * supported capabilities and the size of the HFI table.
	 */
	edx.full = cpuid_edx(CPUID_HFI_LEAF);

	if (!edx.split.capabilities.split.performance) {
		pr_debug("Performance reporting not supported! Not using HFI\n");
		return -ENODEV;
	}

	/*
	 * The number of supported capabilities determines the number of
	 * columns in the HFI table. Exclude the reserved bits.
	 */
	edx.split.capabilities.split.__reserved = 0;
	nr_capabilities = hweight8(edx.split.capabilities.bits);

	/* The number of 4KB pages required by the table */
	hfi_features.nr_table_pages = edx.split.table_pages + 1;

	/*
	 * Capability fields of an HFI class are grouped together. Classes are
	 * contiguous in memory.  Hence, use the number of supported features to
	 * locate a specific class.
	 */
	hfi_features.class_stride = nr_capabilities;

	if (cpu_feature_enabled(X86_FEATURE_ITD)) {
		union cpuid6_ecx ecx;

		ecx.full = cpuid_ecx(CPUID_HFI_LEAF);
		hfi_features.nr_classes = ecx.split.nr_classes;
	} else {
		hfi_features.nr_classes = 1;
	}

	/*
	 * The header contains change indications for each supported feature.
	 * The size of the table header is rounded up to be a multiple of 8
	 * bytes.
	 */
	hfi_features.hdr_size = DIV_ROUND_UP(nr_capabilities *
					     hfi_features.nr_classes, 8) * 8;

	/*
	 * Data of each logical processor is also rounded up to be a multiple
	 * of 8 bytes.
	 */
	hfi_features.cpu_stride = DIV_ROUND_UP(nr_capabilities *
					       hfi_features.nr_classes, 8) * 8;

	return 0;
}

static void hfi_do_pm_enable(void *info)
{
	struct hfi_instance *hfi_instance = info;

	hfi_set_hw_table(hfi_instance);
	hfi_enable();

	hfi_enable_itd_classification();
}

static void hfi_do_pm_disable(void *info)
{
	hfi_disable_itd_classification();

	hfi_disable();
}

static int hfi_pm_notify(struct notifier_block *nb,
			 unsigned long mode, void *unused)
{
	struct hfi_cpu_info *info = &per_cpu(hfi_cpu_info, 0);
	struct hfi_instance *hfi_instance = info->hfi_instance;

	/* HFI may not be in use. */
	if (!hfi_instance)
		return 0;

	/*
	 * Only handle the HFI instance of the package of the boot CPU. The
	 * instances of other packages are handled in the CPU hotplug callbacks.
	 */
	switch (mode) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
	case PM_RESTORE_PREPARE:
		return smp_call_function_single(0, hfi_do_pm_disable,
						NULL, true);

	case PM_POST_RESTORE:
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		return smp_call_function_single(0, hfi_do_pm_enable,
						hfi_instance, true);
	default:
		return -EINVAL;
	}
}

static struct notifier_block hfi_pm_nb = {
	.notifier_call = hfi_pm_notify,
};

void __init intel_hfi_init(void)
{
	struct hfi_instance *hfi_instance;
	int i, j;

	if (register_pm_notifier(&hfi_pm_nb))
		return;

	if (hfi_parse_features())
		return;

	/* There is one HFI instance per die/package. */
	max_hfi_instances = topology_max_packages() *
			    topology_max_die_per_package();

	/*
	 * This allocation may fail. CPU hotplug callbacks must check
	 * for a null pointer.
	 */
	hfi_instances = kcalloc(max_hfi_instances, sizeof(*hfi_instances),
				GFP_KERNEL);
	if (!hfi_instances)
		return;

	for (i = 0; i < max_hfi_instances; i++) {
		hfi_instance = &hfi_instances[i];
		if (!zalloc_cpumask_var(&hfi_instance->cpus, GFP_KERNEL))
			goto err_nomem;
	}

	hfi_updates_wq = create_singlethread_workqueue("hfi-updates");
	if (!hfi_updates_wq)
		goto err_nomem;

	if (!alloc_hfi_ipcc_scores())
		goto err_ipcc;

	hfi_debugfs_register();
	proc_fs_register();

	return;

err_ipcc:
	destroy_workqueue(hfi_updates_wq);

err_nomem:
	for (j = 0; j < i; ++j) {
		hfi_instance = &hfi_instances[j];
		free_cpumask_var(hfi_instance->cpus);
	}

	kfree(hfi_instances);
	hfi_instances = NULL;
}
