// SPDX-License-Identifier: GPL-2.0
/*
 * intel-pem-tpmi: platform excursion monitor enabling
 *
 * Copyright (c) 2022, Intel Corporation.
 * All Rights Reserved.
 *
 */

#define DEBUG
#include <linux/auxiliary_bus.h>
#include <linux/intel_tpmi.h>
#include <linux/bits.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nospec.h>
#include <linux/perf_event.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <asm/cpu_device_id.h>

#include "intel_tpmi_pem_core.h"
#include "../hpm_die_map.h"
#include "../pmt/telemetry.h"

#define PEM_HEADER_VERSION	1
#define PEM_HEADER_INDEX	0
#define PEM_CONTROL_INDEX	8
#define PEM_STATUS_INDEX	16

struct tpmi_pem_instance_info {
	int pkg_id;
	int die_id;
	void __iomem *pem_base;
	int pmt_info_offset;
	struct intel_tpmi_plat_info *plat_info;
	struct auxiliary_device *auxdev;
};

/* Each socket will have multiple die instances */
struct tpmi_pem_struct {
	int pkg_id;
	int number_of_instances;
	struct tpmi_pem_instance_info *instance_info;
};

/* Max number of possible sockets, one instance per socket */
#define	PEM_MAX_INSTANCES	16

struct tpmi_pem_common_struct {
	int max_instance_id;
	struct tpmi_pem_struct __rcu *pem_inst[PEM_MAX_INSTANCES];
};

/*
 * Lock to prevent registration with the pem_core from the
 * client drivers. Also prevents read/write to parameters set
 * via sysfs attributes.
 */
static DEFINE_MUTEX(pem_tpmi_dev_lock);

/* Usage counters to client registered with pem_core */
static int pem_core_usage_count;

/* Store all PEM instances */
static struct tpmi_pem_common_struct pem_common;

/* For CPU online/offline */
int pem_online_id;

/* Mask of CPUs representing a die */
static cpumask_t pem_die_cpu_mask;

/* PMU struct for PEM */
static struct pmu pem_pmu;

/* Usage counters track active perf session in progress */
static int pem_perf_active;

/* Similar macros are used in the other PMU drivers to create attributes */

#define EVENT_VAR(_id)		event_attr_##_id
#define EVENT_PTR(_id)		&event_attr_##_id.attr.attr

#define EVENT_ATTR(_name, _id)                                          \
static struct perf_pmu_events_attr EVENT_VAR(_id) = {                   \
        .attr           = __ATTR(_name, 0444, events_sysfs_show, NULL), \
        .id             = PERF_COUNT_HW_##_id,                          \
        .event_str      = NULL,                                         \
};

#define EVENT_ATTR_STR(_name, v, str)                                   \
static struct perf_pmu_events_attr event_attr_##v = {                   \
        .attr           = __ATTR(_name, 0444, events_sysfs_show, NULL), \
        .id             = 0,                                            \
        .event_str      = str,                                          \
};

#define __PMU_EVENT_GROUP(_name)			\
static struct attribute *attrs_##_name[] = {		\
	&attr_##_name.attr.attr,			\
	NULL,						\
}

#define PMU_EVENT_GROUP(_grp, _name)			\
__PMU_EVENT_GROUP(_name);				\
static struct attribute_group group_##_name = {		\
	.name  = #_grp,					\
	.attrs = attrs_##_name,				\
}

#define DEFINE_PEM_FORMAT_ATTR(_var, _name, _format)		\
static ssize_t __pem_##_var##_show(struct device *dev,	\
				struct device_attribute *attr,	\
				char *page)			\
{								\
	BUILD_BUG_ON(sizeof(_format) >= PAGE_SIZE);		\
	return sprintf(page, _format "\n");			\
}								\
static struct device_attribute format_attr_##_var =		\
	__ATTR(_name, 0444, __pem_##_var##_show, NULL)

enum pem_perf_events {
	PERF_PEM_PEM_ANY = 0,
	PERF_PEM_THERMAL,
	PERF_PEM_EXT_PROCHOT,
	PERF_PEM_PBM,
	PERF_PEM_PL1,
	PERF_PEM_PL1_PECI,
	PERF_PEM_PL1_CFG,
	PERF_PEM_PL2,
	PERF_PEM_PEM_PL2_PECI,
	PERF_PEM_PEM_PL2_CFG,
	PERF_PEM_PPL1,
	PERF_PEM_PPL1_PECI,
	PERF_PEM_PPL1_CFG,
	PERF_PEM_PPL2,
	PERF_PEM_PPL2_PECI,
	PERF_PEM_PPL2_CFG,
	PERF_PEM_PMAX,
	PERF_PEM_PKG_EVENT_MAX,
};

PMU_EVENT_ATTR_STRING(any,  attr_pem_any, "event=0x00");
PMU_EVENT_ATTR_STRING(thermal,  attr_pem_thermal, "event=0x01");
PMU_EVENT_ATTR_STRING(ext_prochot,  attr_pem_ext_prochot, "event=0x02");
PMU_EVENT_ATTR_STRING(pbm,  attr_pem_pbm, "event=0x03");
PMU_EVENT_ATTR_STRING(pl1,  attr_pem_pl1, "event=0x04");
PMU_EVENT_ATTR_STRING(peci,  attr_pem_pl1_peci, "event=0x05");
PMU_EVENT_ATTR_STRING(pl1_cfg,  attr_pem_pl1_cfg, "event=0x06");
PMU_EVENT_ATTR_STRING(pl2,  attr_pem_pl2, "event=0x07");
PMU_EVENT_ATTR_STRING(pl2_peci,  attr_pem_pl2_peci, "event=0x08");
PMU_EVENT_ATTR_STRING(pl2_cfg,  attr_pem_pl2_cfg, "event=0x09");
PMU_EVENT_ATTR_STRING(ppl1,  attr_pem_ppl1, "event=0x0A");
PMU_EVENT_ATTR_STRING(ppl1_peci,  attr_pem_ppl1_peci, "event=0x0B");
PMU_EVENT_ATTR_STRING(ppl1_cfg,  attr_pem_ppl1_cfg, "event=0x0C");
PMU_EVENT_ATTR_STRING(ppl2,  attr_pem_ppl2, "event=0x0D");
PMU_EVENT_ATTR_STRING(ppl2_peci,  attr_pem_ppl2_peci, "event=0x0E");
PMU_EVENT_ATTR_STRING(ppl2_cfg,  attr_pem_ppl2_cfg, "event=0x0F");
PMU_EVENT_ATTR_STRING(pmax,  attr_pem_pmax, "event=0x20");

PMU_EVENT_GROUP(events, pem_any);
PMU_EVENT_GROUP(events, pem_thermal);
PMU_EVENT_GROUP(events, pem_ext_prochot);
PMU_EVENT_GROUP(events, pem_pbm);
PMU_EVENT_GROUP(events, pem_pl1);
PMU_EVENT_GROUP(events, pem_pl1_peci);
PMU_EVENT_GROUP(events, pem_pl1_cfg);
PMU_EVENT_GROUP(events, pem_pl2);
PMU_EVENT_GROUP(events, pem_pl2_peci);
PMU_EVENT_GROUP(events, pem_pl2_cfg);
PMU_EVENT_GROUP(events, pem_ppl1);
PMU_EVENT_GROUP(events, pem_ppl1_peci);
PMU_EVENT_GROUP(events, pem_ppl1_cfg);
PMU_EVENT_GROUP(events, pem_ppl2);
PMU_EVENT_GROUP(events, pem_ppl2_peci);
PMU_EVENT_GROUP(events, pem_ppl2_cfg);
PMU_EVENT_GROUP(events, pem_pmax);

#define for_each_pem_pkg(start, pkg_instance)\
	for (pkg_instance = rcu_dereference(pem_common.pem_inst[start]);\
		start <= pem_common.max_instance_id; ++start)\

#define for_each_pem_die(start, pkg_instance, instance)\
	for (; start < pkg_instance->number_of_instances &&\
		(instance = &pkg_instance->instance_info[start]); ++start)

static ssize_t pem_fet_attr_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct tpmi_pem_instance_info *instance;
	struct tpmi_pem_struct *pkg_instance;
	int i = 0, j = 0, ret = -EIO;

	mutex_lock(&pem_tpmi_dev_lock);

	/* Once an active perf call is in progress, can't change attributes */
	if (pem_perf_active) {
		ret = -EBUSY;
		goto unlock_fet_show;
	}

	rcu_read_lock();

	for_each_pem_pkg(i, pkg_instance) {
		if (!pkg_instance)
			continue;

		for_each_pem_die(j, pkg_instance, instance) {
			u32 val;

			if (!instance->pem_base)
				continue;

			val = intel_tpmi_readq(instance->auxdev,
					       (u8 __iomem *)instance->pem_base +
					       PEM_CONTROL_INDEX);
			ret = sprintf(buf, "%u\n", (val & 0xff) * 100);
			goto unlock_fet_show;
		}
	}

unlock_fet_show:
	rcu_read_unlock();
	mutex_unlock(&pem_tpmi_dev_lock);

	return ret;
}

static ssize_t pem_fet_attr_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct tpmi_pem_instance_info *instance;
	struct tpmi_pem_struct *pkg_instance;
	int i = 0, j = 0, ret = -EIO;
	unsigned int input;

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	input /= 100; /* convert to ratio from MHz */

	mutex_lock(&pem_tpmi_dev_lock);

	if (pem_perf_active) {
		ret = -EBUSY;
		goto unlock_fet_store;
	}

	rcu_read_lock();

	for_each_pem_pkg(i, pkg_instance) {
		if (!pkg_instance)
			continue;
		for_each_pem_die(j, pkg_instance, instance) {
			u32 val;

			if (!instance->pem_base)
				continue;

			val = intel_tpmi_readq(instance->auxdev,
					       (u8 __iomem *)instance->pem_base +
					       PEM_CONTROL_INDEX);
			val &= ~0xff;
			val |= (input & 0xFF);
			intel_tpmi_writeq(instance->auxdev, val,
					  (u8 __iomem *)instance->pem_base + PEM_CONTROL_INDEX);
			ret = count;
		}
	}

unlock_fet_store:
	rcu_read_unlock();
	mutex_unlock(&pem_tpmi_dev_lock);

	return ret;
}

static ssize_t pem_time_window_attr_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct tpmi_pem_instance_info *instance;
	struct tpmi_pem_struct *pkg_instance;
	int i = 0, j = 0, ret = -EIO;

	mutex_lock(&pem_tpmi_dev_lock);

	if (pem_perf_active) {
		ret = -EBUSY;
		goto unlock_tw_show;
	}

	rcu_read_lock();

	for_each_pem_pkg(i, pkg_instance) {
		if (!pkg_instance)
			continue;
		for_each_pem_die(j, pkg_instance, instance) {
			u32 val;

			if (!instance->pem_base)
				continue;

			val = intel_tpmi_readq(instance->auxdev,
					       (u8 __iomem *)instance->pem_base +
					       PEM_CONTROL_INDEX);
			val = (val >> 8) & 0xff;
			/* Valid TW range is 0 to 17 */
			if (val > 17) {
				ret = -EINVAL;
				continue;
			}
			/* tw is specified as 2.3*(2^TW) ms */
			val =  DIV_ROUND_UP(23 * int_pow(2, val),  10);

			ret = sprintf(buf, "%u\n", val);
			goto unlock_tw_show;
		}
	}

unlock_tw_show:
	rcu_read_unlock();
	mutex_unlock(&pem_tpmi_dev_lock);

	return ret;
}

static ssize_t	pem_time_window_attr_store(struct device *dev, struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct tpmi_pem_instance_info *instance;
	struct tpmi_pem_struct *pkg_instance;
	int i = 0, j = 0, ret = -EIO;
	unsigned int input;

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	/* tw is specified as 2.3*(2^TW) ms */
	input = ilog2(input * 10 / 23);
	if (input > 17)
		return -EINVAL;

	mutex_lock(&pem_tpmi_dev_lock);

	if (pem_perf_active) {
		ret = -EBUSY;
		goto unlock_tw_store;
	}

	rcu_read_lock();

	for_each_pem_pkg(i, pkg_instance) {
		if (!pkg_instance)
			continue;
		for_each_pem_die(j, pkg_instance, instance) {
			u32 val;

			if (!instance->pem_base)
				continue;

			val = intel_tpmi_readq(instance->auxdev,
					       (u8 __iomem *)instance->pem_base +
					       PEM_CONTROL_INDEX);
			val &= ~GENMASK(14, 8);
			val |= ((input & 0x7F) << 8);
			intel_tpmi_writeq(instance->auxdev, val,
					  (u8 __iomem *)instance->pem_base + PEM_CONTROL_INDEX);
			ret = count;
		}
	}

unlock_tw_store:
	rcu_read_unlock();
	mutex_unlock(&pem_tpmi_dev_lock);

	return ret;
}

static ssize_t pem_status_attr_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct tpmi_pem_instance_info *instance;
	struct tpmi_pem_struct *pkg_instance;
	int i = 0, j = 0, index = 0;

	mutex_lock(&pem_tpmi_dev_lock);

	if (pem_perf_active) {
		index = -EBUSY;
		goto unlock_status_show;
	}

	rcu_read_lock();

	for_each_pem_pkg(i, pkg_instance) {
		if (!pkg_instance)
			continue;
		for_each_pem_die(j, pkg_instance, instance) {
			u32 val;

			if (!instance->pem_base)
				continue;

			val = intel_tpmi_readq(instance->auxdev,
					       (u8 __iomem *)instance->pem_base +
					       PEM_STATUS_INDEX);
			index += snprintf(&buf[index], PAGE_SIZE - index,
					  "pkg%02d-die%02d:%u\n", i, j, val & 0xff);
		}
	}

unlock_status_show:
	rcu_read_unlock();
	mutex_unlock(&pem_tpmi_dev_lock);

	if (!index)
		return -EIO;

	return index;
}

static ssize_t pem_status_attr_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct tpmi_pem_instance_info *instance;
	struct tpmi_pem_struct *pkg_instance;
	int i = 0, j = 0, ret = -EIO;
	unsigned int input;

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	mutex_lock(&pem_tpmi_dev_lock);

	if (pem_perf_active) {
		ret = -EBUSY;
		goto unlock_status_store;
	}

	rcu_read_lock();

	for_each_pem_pkg(i, pkg_instance) {
		if (!pkg_instance)
			continue;
		for_each_pem_die(j, pkg_instance, instance) {
			if (!instance->pem_base)
				continue;

			intel_tpmi_writeq(instance->auxdev, input,
					  (u8 __iomem *)instance->pem_base + PEM_STATUS_INDEX);
			ret = count;
		}
	}

unlock_status_store:
	rcu_read_unlock();
	mutex_unlock(&pem_tpmi_dev_lock);

	return ret;
}

static ssize_t pem_enable_attr_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct tpmi_pem_instance_info *instance;
	struct tpmi_pem_struct *pkg_instance;
	int i = 0, j = 0, index = 0;

	mutex_lock(&pem_tpmi_dev_lock);

	if (pem_perf_active) {
		index = -EBUSY;
		goto unlock_enable_show;
	}

	rcu_read_lock();

	for_each_pem_pkg(i, pkg_instance) {
		if (!pkg_instance)
			continue;
		for_each_pem_die(j, pkg_instance, instance) {
			u32 val;

			if (!instance->pem_base)
				continue;

			val = intel_tpmi_readq(instance->auxdev,
					       (u8 __iomem *)instance->pem_base +
					       PEM_CONTROL_INDEX);
			index += snprintf(&buf[index], PAGE_SIZE - index,
					  "pkg%02d-die%02d:%d\n", i, j, (val & BIT(31)) ? 1 : 0);
		}
	}

unlock_enable_show:
	rcu_read_unlock();
	mutex_unlock(&pem_tpmi_dev_lock);

	if (!index)
		return -EIO;

	return index;
}

static int pem_feature_enable(unsigned int enable)
{
	struct tpmi_pem_instance_info *instance;
	struct tpmi_pem_struct *pkg_instance;
	int i = 0, j = 0, ret = -EIO;

	/* called from non preemptive context also */
	rcu_read_lock();
	for_each_pem_pkg(i, pkg_instance) {
		if (!pkg_instance)
			continue;

		for_each_pem_die(j, pkg_instance, instance) {
			u32 val;

			if (!instance->pem_base)
				continue;

			val = intel_tpmi_readq(instance->auxdev,
					       (u8 __iomem *)instance->pem_base +
					       PEM_CONTROL_INDEX);
			if (enable)
				val |= BIT(31);
			else
				val &= ~BIT(31);
			intel_tpmi_writeq(instance->auxdev, val,
					  (u8 __iomem *)instance->pem_base + PEM_CONTROL_INDEX);
			ret = 0;
		}
	}
	rcu_read_unlock();

	return ret;
}

/* This attribute will not be upstreamed, but useful during tests */
static ssize_t pem_enable_attr_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	mutex_lock(&pem_tpmi_dev_lock);

	if (pem_perf_active)
		ret = -EBUSY;
	else
		ret = pem_feature_enable(input);

	mutex_unlock(&pem_tpmi_dev_lock);

	if (ret)
		return ret;

	return count;
}

#define PEM_PMU_EVENT_ATTR(_name, _var, _id, _show, _store)		\
static struct perf_pmu_events_attr _var = {				\
	.attr = __ATTR(_name, 0644, _show, _store),			\
	.id   =  _id,							\
};

PEM_PMU_EVENT_ATTR(frequency_excursion_mhz, pem_fet_attr, 0,
		   pem_fet_attr_show, pem_fet_attr_store);

PEM_PMU_EVENT_ATTR(frequency_excursion_time_window_ms, pem_time_window, 1,
		  pem_time_window_attr_show, pem_time_window_attr_store);

PEM_PMU_EVENT_ATTR(frequency_excursion_status, pem_status, 1,
		  pem_status_attr_show, pem_status_attr_store);

PEM_PMU_EVENT_ATTR(frequency_excursion_enable, pem_enable, 1,
		  pem_enable_attr_show, pem_enable_attr_store);

static struct attribute *pem_threshold_attr[] = {
	&pem_fet_attr.attr.attr,
	&pem_time_window.attr.attr,
	&pem_status.attr.attr,
	&pem_enable.attr.attr,
        NULL,
};

static struct attribute_group pem_threshold_group = {
        .attrs  = pem_threshold_attr,
};

static struct attribute *attrs_empty[] = {
        NULL,
};

static struct attribute_group pkg_events_attr_group = {
	.name = "events",
	.attrs = attrs_empty,
};

DEFINE_PEM_FORMAT_ATTR(pkg_event, event, "config:0-16");
static struct attribute *pkg_format_attrs[] = {
	&format_attr_pkg_event.attr,
	NULL,
};
static struct attribute_group pkg_format_attr_group = {
	.name = "format",
	.attrs = pkg_format_attrs,
};

static ssize_t pem_get_attr_cpumask(struct device *dev, struct device_attribute *attr,
				    char *buf);

static DEVICE_ATTR(cpumask, S_IRUGO, pem_get_attr_cpumask, NULL);

static struct attribute *pem_cpumask_attrs[] = {
        &dev_attr_cpumask.attr,
        NULL,
};

static struct attribute_group cpumask_attr_group = {
        .attrs = pem_cpumask_attrs,
};

static const struct attribute_group *pkg_attr_groups[] = {
	&pkg_events_attr_group,
	&pkg_format_attr_group,
	&cpumask_attr_group,
	&pem_threshold_group,
	NULL,
};

static ssize_t pem_get_attr_cpumask(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct pmu *pmu = dev_get_drvdata(dev);

	if (pmu == &pem_pmu)
		return cpumap_print_to_pagebuf(true, buf, &pem_die_cpu_mask);
	else
		return 0;
}

static void pem_pmu_active(void)
{
	mutex_lock(&pem_tpmi_dev_lock);
	++pem_perf_active;
	mutex_unlock(&pem_tpmi_dev_lock);
	pr_debug("%s pem_perf_active:%d\n", __func__, pem_perf_active);
}

static void pem_pmu_deactive(struct perf_event *event)
{
	mutex_lock(&pem_tpmi_dev_lock);
	--pem_perf_active;
	mutex_unlock(&pem_tpmi_dev_lock);
	pr_debug("%s pem_perf_active:%d\n", __func__, pem_perf_active);
}

static int pem_pmu_event_init(struct perf_event *event)
{
	u64 cfg = event->attr.config;
	int cpu;

	pr_debug("%s cpu:%d sample period:%llx event->attr.type:%d event->pmu->type:%d\n", __func__, raw_smp_processor_id(),
		 event->attr.sample_period, event->attr.type, event->pmu->type);

	/* Only process of the type matches what we got from perf_pmu_register() */
	if (event->attr.type != pem_pmu.type) {
		pr_debug("%s cpu%d fail attr type != pmu type\n", __func__,
			raw_smp_processor_id());
		return -ENOENT;
	}

	/* unsupported modes and filters */
	if (event->attr.sample_period) /* no sampling */ {
		pr_debug("%s cpu%d no smaple period\n", __func__, raw_smp_processor_id());
		return -EINVAL;
	}

	if (event->cpu < 0)
		return -EINVAL;

	if (event->pmu == &pem_pmu) {
		if (cfg >= PERF_PEM_PKG_EVENT_MAX) {
			pr_debug("%s cpu%d pkg event mask\n", __func__, raw_smp_processor_id());
			return -EINVAL;
		}
		if (cfg >= PERF_PEM_PKG_EVENT_MAX) {
			pr_debug("%s cpu%d mmio mask\n", __func__, raw_smp_processor_id());
			return -EINVAL;
		}
		event->hw.event_base = cfg;
		cpu = cpumask_any_and(&pem_die_cpu_mask,
				      hpm_get_die_mask(event->cpu));
	} else {
		pr_debug("%s cpu%d pkg no entry\n", __func__, raw_smp_processor_id());
		return -ENOENT;
	}

	if (cpu >= nr_cpu_ids) {
		pr_debug("%s cpu%d pkg nr cpuid\n", __func__, raw_smp_processor_id());
		return -ENODEV;
	}

	event->cpu = cpu;
	event->hw.config = cfg;
	event->hw.idx = -1;
	event->destroy = pem_pmu_deactive;
	pem_pmu_active();
	pr_debug("%s cpu%d success \n", __func__, raw_smp_processor_id());

	return 0;
}

static int pmt_telem_read_counters(struct pci_dev *pci_dev, int instance, u32 guid,
				   u16 sample_id, u16 sample_count, u64 *samples)
{
	/* This function will call PMT interface function */
	pr_debug("guid:%x sample_id:%x sample_count:%x\n", guid, sample_id, sample_count);
	return pmt_telem_read64(pci_dev, guid, 0, sample_id, sample_count, samples);
}

static u32 pem_read_pmt_counter(struct tpmi_pem_instance_info *instance, int index)
{
	u16 sample_id, sample_count;
	struct pci_dev *pci_dev;
	int bus, dev, fn, ret;
	u64 counters[16];
	u32 guid;
	u64 val;

	if (!instance || index > 16)
		return 0;

	if (!instance->pmt_info_offset)
		return 0; /* No info offset field is available */

	val = readq((u8 __iomem *)instance->pem_base + instance->pmt_info_offset * 8);
	guid = val & 0xffffffff;
	sample_id = (val >> 32) & 0xffff;
	sample_count = (val >> 48) & 0xffff;

	bus = instance->plat_info->bus_number;
	dev = instance->plat_info->device_number;
	fn = instance->plat_info->function_number;

	pr_debug("Read from PMT device with B:%x D:%x F:%x\n", bus, dev, fn);
	pr_debug("Read GUID :%x sample id:%x sample_count:%x\n", guid, sample_id, sample_count);
	pci_dev = pci_get_domain_bus_and_slot(0, bus, PCI_DEVFN(dev, fn));
	if (!pci_dev) {
		pr_err("No PCI device instance for B:%x D:%x F:%x\n", bus, dev, fn);
		return 0;
	}

	ret = pmt_telem_read_counters(pci_dev, 0, guid, sample_id, sample_count, counters);
	if (ret) {
		pr_debug("Read Failed GUID :%x sample id:%x sample_count:%x\n", guid, sample_id, sample_count);
		return 0;
	}

	pr_debug("Returning PMT counter at index:%d\n", index);
	return counters[index];
}

static inline u64 pem_pmu_read_counter(struct perf_event *event)
{
	struct tpmi_pem_instance_info *instance;
	struct tpmi_pem_struct *pkg_instance;
	int cpu = raw_smp_processor_id();
	u64 counter = 0;
	int i = 0, pkg, die;

	pr_debug("%s cpu%d\n", __func__, raw_smp_processor_id());
	die = hpm_get_die_id(cpu);
	pkg = topology_physical_package_id(cpu);

	rcu_read_lock();
	for_each_pem_pkg(pkg, pkg_instance) {
		if (!pkg_instance)
			continue;

		for_each_pem_die(i, pkg_instance, instance) {
			u32 val;

			if (!instance->pem_base)
				continue;

			if (instance->pkg_id != pkg || instance->die_id != die)
				continue;

			pr_debug("%s cpu%d base:%lx \n", __func__, raw_smp_processor_id(), event->hw.event_base);
			val = intel_tpmi_readq(instance->auxdev,
					       (u8 __iomem *)instance->pem_base + PEM_STATUS_INDEX);

			if (val & BIT(event->hw.event_base))
				counter += pem_read_pmt_counter(instance, event->hw.event_base);
		}
	}
	rcu_read_unlock();

	return counter;
}

static void pem_pmu_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_raw_count, new_raw_count;

	pr_debug("%s cpu%d\n", __func__, raw_smp_processor_id());
again:
	prev_raw_count = local64_read(&hwc->prev_count);
	new_raw_count = pem_pmu_read_counter(event);

	if (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
			    new_raw_count) != prev_raw_count)
		goto again;

	local64_add(new_raw_count - prev_raw_count, &event->count);
}

static int pem_monitor_enable(int enable)
{
	pr_debug("%s cpu%d\n", __func__, raw_smp_processor_id());
	return pem_feature_enable(enable);
}

static void pem_pmu_event_start(struct perf_event *event, int mode)
{
	pr_debug("%s\n", __func__);
	pem_monitor_enable(1);
	local64_set(&event->hw.prev_count, pem_pmu_read_counter(event));
}

static void pem_pmu_event_stop(struct perf_event *event, int mode)
{
	pr_debug("%s\n", __func__);
	pem_monitor_enable(0);
	pem_pmu_event_update(event);
}

static void pem_pmu_event_del(struct perf_event *event, int mode)
{
	pem_pmu_event_stop(event, PERF_EF_UPDATE);
	pr_debug("%s pem_perf_active:%d\n", __func__, pem_perf_active);
}

static int pem_pmu_event_add(struct perf_event *event, int mode)
{
	pr_debug("%s pem_perf_active:%d\n", __func__, pem_perf_active);
	if (mode & PERF_EF_START)
		pem_pmu_event_start(event, mode);

	return 0;
}

static const struct attribute_group *pkg_attr_update[] = {
	&group_pem_any,
	&group_pem_thermal,
	&group_pem_ext_prochot,
	&group_pem_pbm,
	&group_pem_pl1,
	&group_pem_pl1_peci,
	&group_pem_pl1_cfg,
	&group_pem_pl2,
	&group_pem_pl2_peci,
	&group_pem_pl2_cfg,
	&group_pem_ppl1,
	&group_pem_ppl1_peci,
	&group_pem_ppl1_cfg,
	&group_pem_ppl2,
	&group_pem_ppl2_peci,
	&group_pem_ppl2_cfg,
	&group_pem_pmax,
	NULL,
};

static struct pmu pem_pmu = {
	.attr_groups	= pkg_attr_groups,
	.attr_update	= pkg_attr_update,
	.name		= "pnp_excursion_monitor",
	.task_ctx_nr	= perf_invalid_context,
	.event_init	= pem_pmu_event_init,
	.add		= pem_pmu_event_add,
	.del		= pem_pmu_event_del,
	.start		= pem_pmu_event_start,
	.stop		= pem_pmu_event_stop,
	.read		= pem_pmu_event_update,
	.capabilities	= PERF_PMU_CAP_NO_INTERRUPT | PERF_PMU_CAP_NO_EXCLUDE,
	.module		= THIS_MODULE,
};

/*
 * Check if exiting cpu is the designated reader. If so migrate the
 * events when there is a valid target available
 */
static int pem_cpu_exit(unsigned int cpu)
{
	unsigned int target;

	if (cpumask_test_and_clear_cpu(cpu, &pem_die_cpu_mask)) {

		target = cpumask_any_but(hpm_get_die_mask(cpu), cpu);
		/* Migrate events if there is a valid target */
		if (target < nr_cpu_ids) {
			cpumask_set_cpu(target, &pem_die_cpu_mask);
			perf_pmu_migrate_context(&pem_pmu, cpu, target);
		}
	}
	return 0;
}

static int pem_cpu_init(unsigned int cpu)
{
	unsigned int target;

	/*
	 * If this is the first online thread of that package/die, set it
	 * in the package cpu mask as the designated reader.
	 */
	target = cpumask_any_and(&pem_die_cpu_mask,
				 hpm_get_die_mask(cpu));
	if (target >= nr_cpu_ids)
		cpumask_set_cpu(cpu, &pem_die_cpu_mask);

	return 0;
}

int tpmi_pem_dev_add(struct auxiliary_device *auxdev)
{
	struct intel_tpmi_plat_info *plat_info;
	struct tpmi_pem_struct *tpmi_pem;
	int i, pkg = 0, inst = 0;
	int num_resources;

	plat_info = tpmi_get_platform_data(auxdev);
	if (!plat_info) {
		dev_info(&auxdev->dev, "No platform info\n");
		return -EINVAL;
	}

	pkg = plat_info->package_id;
	if (pkg >= PEM_MAX_INSTANCES) {
		dev_info(&auxdev->dev, "Invalid package id :%d\n", pkg);
		return -EINVAL;
	}

	if (pem_common.pem_inst[pkg])
		return -EEXIST;

	num_resources = tpmi_get_resource_count(auxdev);
	dev_dbg(&auxdev->dev, "Number of resources:%x \n", num_resources);

	if (!num_resources)
		return -EINVAL;

	tpmi_pem = devm_kzalloc(&auxdev->dev, sizeof(*tpmi_pem), GFP_KERNEL);
	if (!tpmi_pem)
		return -ENOMEM;

	tpmi_pem->instance_info = devm_kcalloc(&auxdev->dev, num_resources,
					    sizeof(*tpmi_pem->instance_info),
					    GFP_KERNEL);
	if (!tpmi_pem->instance_info)
		return -ENOMEM;

	tpmi_pem->number_of_instances = num_resources;

	if (plat_info)
		pkg = plat_info->package_id;

	tpmi_pem->pkg_id = pkg;

	for (i = 0; i < num_resources; ++i) {
		struct resource *res;
		int pem_header_ver;
		u32 val;

		res = tpmi_get_resource_at_index(auxdev, i);
		if (!res)
			continue;

		tpmi_pem->instance_info[i].pem_base = devm_ioremap_resource(&auxdev->dev, res);
		if (IS_ERR(tpmi_pem->instance_info[i].pem_base))
			return PTR_ERR(tpmi_pem->instance_info[i].pem_base);

		val = readl(tpmi_pem->instance_info[i].pem_base);

		pem_header_ver = val & 0xff;
		if (pem_header_ver != PEM_HEADER_VERSION) {
			dev_err(&auxdev->dev, "PEM: Unsupported version:%d\n", pem_header_ver);
			devm_iounmap(&auxdev->dev, tpmi_pem->instance_info[i].pem_base);
			tpmi_pem->instance_info[i].pem_base = NULL;
			continue;
		}

		tpmi_pem->instance_info[i].pmt_info_offset = (val >> 8) & 0xff;
		tpmi_pem->instance_info[i].pkg_id = pkg;
		tpmi_pem->instance_info[i].die_id = i;
		tpmi_pem->instance_info[i].plat_info = plat_info;
		tpmi_pem->instance_info[i].auxdev = auxdev;

		++inst;
	}

	if (!inst)
		return -ENODEV;

	auxiliary_set_drvdata(auxdev, tpmi_pem);

	mutex_lock(&pem_tpmi_dev_lock);
	rcu_assign_pointer(pem_common.pem_inst[pkg], tpmi_pem);

	if (pem_common.max_instance_id < pkg)
		pem_common.max_instance_id = pkg;
	mutex_unlock(&pem_tpmi_dev_lock);

	pm_runtime_enable(&auxdev->dev);
	pm_runtime_set_autosuspend_delay(&auxdev->dev, 2000);
	pm_runtime_use_autosuspend(&auxdev->dev);
	pm_runtime_put(&auxdev->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(tpmi_pem_dev_add);

void tpmi_pem_dev_remove(struct auxiliary_device *auxdev)
{
	struct tpmi_pem_struct *tpmi_pem = auxiliary_get_drvdata(auxdev);

	mutex_lock(&pem_tpmi_dev_lock);
	RCU_INIT_POINTER(pem_common.pem_inst[tpmi_pem->pkg_id], NULL);
	mutex_unlock(&pem_tpmi_dev_lock);

	synchronize_rcu();

	pm_runtime_get_sync(&auxdev->dev);
	pm_runtime_put_noidle(&auxdev->dev);
	pm_runtime_disable(&auxdev->dev);
}
EXPORT_SYMBOL_GPL(tpmi_pem_dev_remove);

static inline void pem_cleanup(void)
{
	cpuhp_remove_state(pem_online_id);
	perf_pmu_unregister(&pem_pmu);
}

int tpmi_pem_init(void)
{
	int ret;

	mutex_lock(&pem_tpmi_dev_lock);

	if (pem_core_usage_count) {
		++pem_core_usage_count;
		goto init_done;
	}

	pem_online_id = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "perf/x86/pem:online", pem_cpu_init, pem_cpu_exit);
	ret = perf_pmu_register(&pem_pmu, pem_pmu.name, -1);
	if (ret) {
		pr_debug("Failed to register pem pkg pmu\n");
		pem_cleanup();
		return ret;
	}

	++pem_core_usage_count;

	mutex_unlock(&pem_tpmi_dev_lock);

	return 0;
init_done:
	mutex_unlock(&pem_tpmi_dev_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(tpmi_pem_init);

void tpmi_pem_exit(void)
{
	mutex_lock(&pem_tpmi_dev_lock);

	if (pem_core_usage_count)
		--pem_core_usage_count;

	if (!pem_core_usage_count) {
		cpuhp_remove_state(pem_online_id);
		perf_pmu_unregister(&pem_pmu);
	}

	mutex_unlock(&pem_tpmi_dev_lock);
}
EXPORT_SYMBOL_GPL(tpmi_pem_exit);

MODULE_LICENSE("GPL v2");
