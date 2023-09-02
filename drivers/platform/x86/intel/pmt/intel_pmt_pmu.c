// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Platform Monitory Technology Telemetry perf pmu events support
 *
 * Copyright (c) 2020, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include <linux/perf_event.h>
#include "telemetry.h"

/*extract n bits starting at position i from x */
#define GET_BITS(x, lsb, msb) (((u64)(x) & GENMASK_ULL(msb, lsb)) >> lsb)

/*---------------------------------------------
 * sysfs format attributes
 *---------------------------------------------
 */
PMU_FORMAT_ATTR(offset,		"config:0-15");
PMU_FORMAT_ATTR(lsb,		"config:16-23");
PMU_FORMAT_ATTR(msb,		"config:24-31");
PMU_FORMAT_ATTR(guid,		"config1:0-31");

static struct pmu intel_pmt_pmu;

static struct attribute *intel_pmt_pmu_format_attrs[] = {
	&format_attr_offset.attr,
	&format_attr_lsb.attr,
	&format_attr_msb.attr,
	&format_attr_guid.attr,
	NULL
};

static struct attribute_group intel_pmt_pmu_format_group = {
	.name = "format",
	.attrs = intel_pmt_pmu_format_attrs,
};

/*---------------------------------------------
 * sysfs cpumask attributes
 *---------------------------------------------
 */
static cpumask_t intel_pmt_pmu_cpu_mask;

static ssize_t
cpumask_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &intel_pmt_pmu_cpu_mask);
}
static DEVICE_ATTR_RO(cpumask);

static struct attribute *intel_pmt_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL
};

static struct attribute_group intel_pmt_pmu_cpumask_attr_group = {
	.attrs = intel_pmt_pmu_cpumask_attrs,
};

static const struct attribute_group *intel_pmt_pmu_groups[] = {
	&intel_pmt_pmu_format_group,
	&intel_pmt_pmu_cpumask_attr_group,
	NULL
};

static int intel_pmt_pmu_event_read(struct perf_event *event, u64 *now)
{
	int ret = 0;
	u64 data;
	u8 lsb, msb;
	u32 offset, qword;
	struct telem_endpoint *ep;

	offset = GET_BITS(event->attr.config, 0, 15);
	qword = (offset / 8);

	ep = (struct telem_endpoint *)event->pmu_private;
	ret = pmt_telem_read(ep, qword, &data, 1);

	if (ret) {
		pr_debug("intel_pmt_pmu: couldn't read offset: %u\n", offset);
		return ret;
	}

	lsb = GET_BITS(event->attr.config, 16, 23);
	msb = GET_BITS(event->attr.config, 24, 31);
	*now = GET_BITS(data, lsb, msb);

	return 0;
}

static int intel_pmt_pmu_event_init(struct perf_event *event)
{
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* unsupported modes and filters */
	if (event->attr.sample_period) /* no sampling */
		return -EINVAL;

	return 0;
}

static void intel_pmt_pmu_event_update(struct perf_event *event)
{
	u64 prev, now;
	s64 delta;

	prev = local64_read(&event->hw.prev_count);
	intel_pmt_pmu_event_read(event, &now);
	delta = (prev <= now) ? now - prev : (-1ULL - prev) + now + 1;

	local64_add(delta, &event->count);
	local64_set(&event->hw.prev_count, now);
}

static void intel_pmt_pmu_start(struct perf_event *event, int flags)
{
	u64 data;

	intel_pmt_pmu_event_read(event, &data);
	local64_set(&event->hw.prev_count, data);
}

static void intel_pmt_pmu_stop(struct perf_event *event, int flags)
{
	intel_pmt_pmu_event_update(event);
}

static int intel_pmt_pmu_add(struct perf_event *event, int flags)
{
	u64 data;
	u32 guid = event->attr.config1;
	static struct telem_endpoint *ep = NULL;

	ep = pmt_telem_find_and_register_endpoint(NULL, guid, 0);
	if (IS_ERR(ep)) {
		pr_debug("intel_pmt_pmu: couldn't get telem endpoint\n");
		return PTR_ERR(ep);
	}

	pr_debug("intel_pmt_pmu: Registered telem endpoint for GUID:%x\n", guid);
	event->pmu_private = ep;

	if (intel_pmt_pmu_event_read(event, &data)) {
		pr_debug("intel_pmt_pmu: intel_pmt_pmu_event_read failed\n");
		pmt_telem_unregister_endpoint((struct telem_endpoint *)event->pmu_private);
		return -EINVAL;
	}

	if (flags & PERF_EF_START)
		intel_pmt_pmu_start(event, flags);

	return 0;
}

static void intel_pmt_pmu_del(struct perf_event *event, int flags)
{
	pmt_telem_unregister_endpoint((struct telem_endpoint *)event->pmu_private);
	intel_pmt_pmu_stop(event, PERF_EF_UPDATE);
}

static int intel_pmt_pmu_cpu_online(unsigned int cpu)
{
	if (cpumask_empty(&intel_pmt_pmu_cpu_mask))
		cpumask_set_cpu(cpu, &intel_pmt_pmu_cpu_mask);

	return 0;
}

static int intel_pmt_pmu_cpu_offline(unsigned int cpu)
{
	int target;

	if (!cpumask_test_and_clear_cpu(cpu, &intel_pmt_pmu_cpu_mask))
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);

	if (target < nr_cpu_ids)
		cpumask_set_cpu(target, &intel_pmt_pmu_cpu_mask);
	else
		return 0;

	perf_pmu_migrate_context(&intel_pmt_pmu, cpu, target);

	return 0;
}

static int nr_intel_pmt_pmu;

static int intel_pmt_pmu_cpuhp_setup(struct pmu *pmt_pmu)
{
	int ret;

	if (nr_intel_pmt_pmu++)
		return 0;

	ret = cpuhp_setup_state(CPUHP_AP_PERF_X86_INTEL_PMT_PERF_ONLINE,
				"drivers/platform/x86/intel/pmt/telemetry:online",
				intel_pmt_pmu_cpu_online,
				intel_pmt_pmu_cpu_offline);
	if (ret)
		nr_intel_pmt_pmu = 0;

	return ret;
}

static void intel_pmt_pmu_cpuhp_free(struct pmu *pmt_pmu)
{
	if (--nr_intel_pmt_pmu)
		return;

	cpuhp_remove_state(CPUHP_AP_PERF_X86_INTEL_PMT_PERF_ONLINE);
}

static struct pmu intel_pmt_pmu = {
		.task_ctx_nr	= perf_sw_context,
		.attr_groups	= intel_pmt_pmu_groups,
		.event_init	= intel_pmt_pmu_event_init,
		.add		= intel_pmt_pmu_add,
		.del		= intel_pmt_pmu_del,
		.start		= intel_pmt_pmu_start,
		.stop		= intel_pmt_pmu_stop,
		.read		= intel_pmt_pmu_event_update,
		.capabilities	= PERF_PMU_CAP_NO_INTERRUPT | PERF_PMU_CAP_NO_EXCLUDE,
};

void pmt_telem_pmu_unregister(void)
{
	intel_pmt_pmu_cpuhp_free(&intel_pmt_pmu);
	perf_pmu_unregister(&intel_pmt_pmu);
}

void pmt_pmu_register(void)
{
	if (perf_pmu_register(&intel_pmt_pmu, "intel_pmt", -1))
		goto err;

	if (intel_pmt_pmu_cpuhp_setup(&intel_pmt_pmu))
		goto unregister;

	return;

unregister:
	perf_pmu_unregister(&intel_pmt_pmu);
err:
	pr_err("Failed to register PMU for intel_pmt_pmu");
}


