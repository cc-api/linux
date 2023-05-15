// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic core lockstep.
 */

#include <linux/bitfield.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/sched/smt.h>
#include <linux/stop_machine.h>

#include <asm/cpu.h>
#include <asm/msr.h>

#undef pr_fmt
#define pr_fmt(fmt) "lockstep: " fmt

#define MSR_IA32_DLSM_CMD			0x2b0
#define CMD_DEACTIVATE				0
#define CMD_ACTIVATE				BIT(0)
#define CMD_ROLE_ACTIVE				0
#define CMD_ROLE_SHADOW				BIT(1)
#define CMD_BINTBRK_ENABLE			BIT(2)
#define CMD_MCCTL_ENABLE			BIT(3)
#define CMD_CORR_MISCO_SEVERITY			BIT(4)
#define CMD_POISON_MISCO			BIT(5)
#define CMD_SRAR_MISCO				BIT(6)

#define MSR_IA32_DLSM_DEACTIVATE_STATUS		0x2b1
#define DEACTIVATE_STATUS_SEVT			BIT(0)
#define DEACTIVATE_STATUS_SWI			BIT(1)
#define DEACTIVATE_STATUS_MISCO			BIT(2)
#define DEACTIVATE_STATUS_CORR_ERR_A		BIT(3)
#define DEACTIVATE_STATUS_CORR_ERR_S		BIT(4)
#define DEACTIVATE_STATUS_ERROR_CODE		GENMASK(63, 32)

#define MSR_IA32_DLSM_ACTIVATE_STATUS		0x2b2
#define PEER_IN_WF_DLSM				BIT(0)
#define I_AM_IN_DLSM				BIT(1)
#define PEER_ABORTED_ENTRY			BIT(2)
#define I_ABORTED_ENTRY				BIT(3)

#define MSR_IA32_DLSM_CAPABILITY		0x2b3
#define CAP_DLCS_LEVEL_TYPE			GENMASK(7, 0)
#define DLCS_LEVEL_TYPE_SMT			1
#define DLCS_LEVEL_TYPE_CORE			2
#define CAP_CAN_BE_ACTIVE			BIT(8)
#define CAP_CAN_BE_SHADOW			BIT(9)
#define CAP_CORR_MISCO_SEVERITY			BIT(10)
#define CAP_POISON_MISCO			BIT(11)
#define CAP_SRAR_MISCO				BIT(12)
#define CAP_PEER_TOPOLOGY_ID			GENMASK(63, 32)

/* CPU roles */
#define ROLE_ACTIVE				BIT(0)
#define ROLE_SHADOW				BIT(1)

struct lockstep_info {
	int		role;
	int		peer_cpu;
	bool		init;
};

static DEFINE_PER_CPU(struct lockstep_info, info);

static ssize_t role_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct lockstep_info *li = per_cpu_ptr(&info, dev->id);

	switch (li->role) {
	case ROLE_ACTIVE:
		return sysfs_emit(buf, "%s\n", "active");
	case ROLE_SHADOW:
		return sysfs_emit(buf, "%s\n", "shadow");
	}
	return 0;
}
static DEVICE_ATTR_RO(role);

static ssize_t peer_cpu_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lockstep_info *li = per_cpu_ptr(&info, dev->id);

	return sysfs_emit(buf, "%d\n", li->peer_cpu);
}
static DEVICE_ATTR_RO(peer_cpu);

static struct attribute *lockstep_common_attrs[] = {
	&dev_attr_role.attr,
	&dev_attr_peer_cpu.attr,
	NULL
};

static const struct attribute_group lockstep_common_attr_group = {
	.name = "lockstep",
	.attrs = lockstep_common_attrs,
};

/* Is there a better way to do this? */
static int apic_to_cpu(u16 apicid)
{
	int cpu;

	for_each_online_cpu(cpu)
		if (cpu_data(cpu).apicid == apicid)
			return cpu;

	return -1;
}

/*
 * Assuming support for only core level lockstep and SMT bit shift of 1.
 * TODO: Eventually rebase this code on the tglx's topology changes and get the
 * bit shifts needed or use cached information from CPUID leaf 0x1F.
 */
#define SMT_SHIFT 1

static int initialize_lockstep_info(void)
{
	int role = 0;
	u64 reg;

	rdmsrl(MSR_IA32_DLSM_CAPABILITY, reg);

	if (FIELD_GET(CAP_CAN_BE_SHADOW, reg))
		role |= ROLE_SHADOW;
	if (FIELD_GET(CAP_CAN_BE_ACTIVE, reg))
		role |= ROLE_ACTIVE;

	/* Only expect a core to be either active or shadow but not both. */
	if (role == (ROLE_SHADOW | ROLE_ACTIVE))
		return -EINVAL;

	this_cpu_write(info.role, role);

	this_cpu_write(info.peer_cpu,
		       apic_to_cpu(FIELD_GET(CAP_PEER_TOPOLOGY_ID, reg) << SMT_SHIFT));

	this_cpu_write(info.init, true);

	return 0;
}

static int lockstep_add_dev(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);
	struct lockstep_info *li = per_cpu_ptr(&info, cpu);
	int ret = 0;

	if (!li->init) {
		ret = initialize_lockstep_info();
		if (ret)
			return ret;

		ret = sysfs_create_group(&dev->kobj, &lockstep_common_attr_group);
	}

	return ret;
}

static int lockstep_remove_dev(unsigned int cpu)
{
	return 0;
}

static int __init lockstep_sysfs_init(void)
{
	u64 reg;

	if (!cpu_feature_enabled(X86_FEATURE_LOCKSTEP))
		return -ENODEV;

	/* TODO: Add a check for other non supported configurations */
	if (sched_smt_active()) {
		pr_info("Only Non-SMT(Non-HT) configuration is supported\n");
		return -ENODEV;
	}

	rdmsrl(MSR_IA32_DLSM_CAPABILITY, reg);
	if (FIELD_GET(CAP_DLCS_LEVEL_TYPE, reg) != DLCS_LEVEL_TYPE_CORE) {
		pr_info("Only core level lockstep is supported\n");
		return -ENODEV;
	}

	/*
	 * Check: Should this hotplug callback be in the ONLINE phase or the
	 * PREPARE phase? Also, is there a need to introduce a new cpuhp_state
	 * instead of using the dynamic one?
	 */
	return cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				 "lockstep:online",
				 lockstep_add_dev,
				 lockstep_remove_dev);
}

device_initcall(lockstep_sysfs_init);
