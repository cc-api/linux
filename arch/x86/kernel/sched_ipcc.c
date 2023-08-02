// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel support for scheduler IPC classes
 *
 * Copyright (c) 2023, Intel Corporation.
 *
 * Author: Ricardo Neri <ricardo.neri-calderon@linux.intel.com>
 *
 * On hybrid processors, the architecture differences between types of CPUs
 * lead to different number of retired instructions per cycle (IPC). IPCs may
 * differ further by classes of instructions.
 *
 * The scheduler assigns an IPC class to every task with arch_update_ipcc()
 * from data that hardware provides. Implement this interface for x86.
 *
 * See kernel/sched/sched.h for details.
 */

#include <linux/sched.h>
#ifdef CONFIG_SCHED_DEBUG
#include <linux/sched/debug.h>
#endif

#include <asm/intel-family.h>
#include <asm/hfi.h>
#include <asm/topology.h>

#ifdef CONFIG_DEBUG_FS
extern unsigned long __percpu *hfi_ipcc_history;

/*
 * Caller must convert from HFI to IPC classes.
 *
 * Must be called from the CPU to which the the history will be logged.
 * This condition is met if called via the scheduler user tick.
 */
static void log_ipcc_history(u8 ipcc)
{
	unsigned long *history;

	if (!hfi_ipcc_history)
		return;

	history = per_cpu_ptr(hfi_ipcc_history, smp_processor_id());
	history[ipcc]++;
}
#else
static void log_ipcc_history(u8 ipcc) { }
#endif

#define CLASS_DEBOUNCER_SKIPS 4
#ifdef CONFIG_DEBUG_FS
unsigned long itd_class_debouncer_skips = 4; /* CLASS_DEBOUNCER_SKIPS */
#endif

/**
 * debounce_and_update_class() - Process and update a task's classification
 *
 * @p:		The task of which the classification will be updated
 * @new_ipcc:	The new IPC classification
 *
 * Update the classification of @p with the new value that hardware provides.
 * Only update the classification of @p if it has been the same during
 * CLASS_DEBOUNCER_SKIPS consecutive ticks.
 */
static void debounce_and_update_class(struct task_struct *p, u8 new_ipcc)
{
	union itd_ipcc *itd_ipcc = (union itd_ipcc *)&p->ipcc;
	u16 debounce_skip;

	/* The class of @p changed. Only restart the debounce counter. */
	if (itd_ipcc->split.class_tmp != new_ipcc) {
		itd_ipcc->split.counter = 1;
		goto out;
	}

	/*
	 * The class of @p did not change. Update it if it has been the same
	 * for CLASS_DEBOUNCER_SKIPS user ticks.
	 */
	debounce_skip = itd_ipcc->split.counter + 1;
#ifdef CONFIG_DEBUG_FS
	if (debounce_skip < itd_class_debouncer_skips)
#else
	if (debounce_skip < CLASS_DEBOUNCER_SKIPS)
#endif
		itd_ipcc->split.counter++;
	else
		itd_ipcc->split.class = new_ipcc;

out:
	itd_ipcc->split.class_tmp = new_ipcc;
}

static bool classification_is_accurate(u8 hfi_class, bool smt_siblings_idle)
{
	switch (boot_cpu_data.x86_model) {
	case INTEL_FAM6_ALDERLAKE:
	case INTEL_FAM6_ALDERLAKE_L:
	case INTEL_FAM6_RAPTORLAKE:
	case INTEL_FAM6_RAPTORLAKE_P:
	case INTEL_FAM6_RAPTORLAKE_S:
	case INTEL_FAM6_METEORLAKE:
	case INTEL_FAM6_METEORLAKE_L:
		if (hfi_class == 3 || hfi_class == 2 || smt_siblings_idle)
			return true;

		return false;

	default:
		return false;
	}
}

void intel_update_ipcc(struct task_struct *curr)
{
	u8 hfi_class;
	bool idle;
	int ret;

	ret = intel_hfi_read_classid(&hfi_class);

	if (ret == -EINVAL)
		log_ipcc_history(IPC_CLASS_UNCLASSIFIED);

	if (ret)
		return;

	log_ipcc_history(hfi_class + 1);

	/*
	 * 0 is a valid classification for Intel Thread Director. A scheduler
	 * IPCC class of 0 means that the task is unclassified. Adjust.
	 */
	idle = sched_smt_siblings_idle(task_cpu(curr));
	if (classification_is_accurate(hfi_class, idle))
		debounce_and_update_class(curr, hfi_class + 1);
}

long intel_get_ipcc_score(unsigned int ipcc, int cpu)
{
	union itd_ipcc itd_ipcc;

	itd_ipcc.full = ipcc;

	return intel_hfi_get_ipcc_score(itd_ipcc.split.class, cpu);
}

#ifdef CONFIG_SCHED_DEBUG
unsigned int arch_ipcc_of(struct task_struct *p)
{
	union itd_ipcc itd_ipcc;

	itd_ipcc.full = p->ipcc;

	return itd_ipcc.split.class;
}
#endif
