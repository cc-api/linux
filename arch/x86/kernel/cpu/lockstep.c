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

#define CMD_ACTIVE_ENABLE			(CMD_ACTIVATE | CMD_ROLE_ACTIVE)
#define CMD_SHADOW_ENABLE			(CMD_ACTIVATE | CMD_ROLE_SHADOW)
#define CMD_DISABLE				CMD_DEACTIVATE

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

/* User Input */
#define USER_LOCKSTEP_ENABLE			1
#define USER_LOCKSTEP_DISABLE			0

struct lockstep_info {
	int		role;
	int		peer_cpu;
	u64		session;
	u32		break_reason;
	u32		break_error_code;
	bool		init;
	bool		enable;
	bool		offline_in_progress;
	bool		online_in_progress;
};

static DEFINE_PER_CPU(struct lockstep_info, info);

static int lockstep_active_enable(void *v)
{
	u64 val;

	rdmsrl(MSR_IA32_DLSM_ACTIVATE_STATUS, val);
	if (val != PEER_IN_WF_DLSM) {
		pr_info("Expected peer_cpu to be waiting for us\n");
		return -EBUSY;
	}

	pr_info("active CPU%d is ready for lockstep\n", raw_smp_processor_id());
	wrmsrl(MSR_IA32_DLSM_CMD, CMD_ACTIVE_ENABLE);

	rdmsrl(MSR_IA32_DLSM_ACTIVATE_STATUS, val);
	if (val != I_AM_IN_DLSM) {
		pr_info("Active cpu expected to be in lockstep\n");
		return -EAGAIN;
	}

	return 0;
}

static int lockstep_break(void *v)
{
	u64 activate_s, deactivate_s;

	rdmsrl(MSR_IA32_DLSM_DEACTIVATE_STATUS, deactivate_s);
	if (deactivate_s != 0) {
		rdmsrl(MSR_IA32_DLSM_ACTIVATE_STATUS, activate_s);
		pr_info("Somehow Lockstep has already been deactivated. Activate status: %llx Deactivate status:%llx\n",
			activate_s, deactivate_s);
	}

	pr_info("Deactivating lockstep on active CPU%d\n", raw_smp_processor_id());
	wrmsrl(MSR_IA32_DLSM_CMD, CMD_DISABLE);

	rdmsrl(MSR_IA32_DLSM_DEACTIVATE_STATUS, deactivate_s);
	if (deactivate_s != DEACTIVATE_STATUS_SWI)
		pr_info("Lockstep deactivated due to an unexpected reason. Deactivate status:%llx\n",
			deactivate_s);

	return 0;
}

void lockstep_shadow_enable(void)
{
	/* Is this a legacy CPU offline operation or a lockstep offline? */
	if (this_cpu_read(info.offline_in_progress) != true)
		return;

	pr_info("shadow CPU%d is ready for lockstep\n", raw_smp_processor_id());
	/*
	 * Do we need to check if the SHADOW really went into the right state?
	 * It seems the error handling might need to be done on the ACTIVE
	 * CPU's side since this side is no longer expected to be responsive.
	 * Would the status on the ACTIVE side say PEER_ABORTED_ENTRY if this
	 * step fails for some reason?
	 *
	 * Also, what should be done if the the shadow exits the state
	 * abruptly? Is the expectation that after lockstep activation, any
	 * deactivation on the SHADOW will always generate an interrupt on the
	 * ACTIVE?
	 */
	wrmsrl(MSR_IA32_DLSM_CMD, CMD_SHADOW_ENABLE);
}

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

static ssize_t enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lockstep_info *li = per_cpu_ptr(&info, dev->id);

	return sysfs_emit(buf, "%d\n", li->enable);
}

static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct lockstep_info *li = per_cpu_ptr(&info, dev->id);
	int active_cpu, shadow_cpu;
	struct device *shadow_dev;
	bool val;
	int ret;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	/* Nothing to do if CPU cores are already in desired state */
	if (li->enable == val)
		return count;

	active_cpu = dev->id;
	shadow_cpu = li->peer_cpu;
	shadow_dev = get_cpu_device(shadow_cpu);

	ret = lock_device_hotplug_sysfs();
	if (ret)
		return ret;

	if (val == USER_LOCKSTEP_ENABLE) {

		if (!cpu_online(shadow_cpu)) {
			pr_info("Error while activating lockstep: Shadow cpu is not online\n");
			ret = -EBUSY;
			goto exit;
		}

		per_cpu_ptr(&info, shadow_cpu)->offline_in_progress = true;
		ret = device_offline(shadow_dev);
		per_cpu_ptr(&info, shadow_cpu)->offline_in_progress = false;
		if (ret)
			goto exit;

		ret = stop_one_cpu(active_cpu, lockstep_active_enable, NULL);
		if (ret) {
			pr_info("Error while activating lockstep\n");
			device_online(shadow_dev);
			goto exit;
		}

	} else { /* USER_LOCKSTEP_DISABLE */
		/* Check: Should the SHADOW be brought online even if stop_one_cpu() has an error on ACTIVE? */
		ret = stop_one_cpu(active_cpu, lockstep_break, NULL);
		if (ret)
			goto exit;

		per_cpu_ptr(&info, li->peer_cpu)->session += 1;

		per_cpu_ptr(&info, shadow_cpu)->online_in_progress = true;
		ret = device_online(shadow_dev);
		per_cpu_ptr(&info, shadow_cpu)->online_in_progress = false;
		if (ret) {
			pr_info("Error while onlining shadow cpu\n");
			goto exit;
		}
	}

	per_cpu_ptr(&info, active_cpu)->enable = val;
	per_cpu_ptr(&info, shadow_cpu)->enable = val;
	ret = count;

exit:
	unlock_device_hotplug();
	return ret;
}
static DEVICE_ATTR_RW(enable);

static ssize_t session_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%lld\n", per_cpu_ptr(&info, dev->id)->session);
}
static DEVICE_ATTR_RO(session);

static int lockstep_update_status(void * arg)
{
	struct lockstep_info * info = (struct lockstep_info *)arg;
	u64 status;

	rdmsrl(MSR_IA32_DLSM_DEACTIVATE_STATUS, status);

	/* TODO: Use macro masks */
	info->break_reason = status & 0xFFFFFFFF;
	info->break_error_code = status >> 32;

	return 0;
}

static ssize_t break_reason_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lockstep_info *li = per_cpu_ptr(&info, dev->id);
	int ret;

	ret = lock_device_hotplug_sysfs();
	if (ret)
		goto error;

	/* Check can we use something other than stop_one_cpu()? */
	ret = stop_one_cpu(dev->id, lockstep_update_status, (void *)li);
	if (ret)
		goto exit;

	unlock_device_hotplug();

	return sysfs_emit(buf, "0x%x\n", li->break_reason);

exit:
	unlock_device_hotplug();
error:
	return sysfs_emit(buf, "-1\n");
}
static DEVICE_ATTR_RO(break_reason);

static ssize_t break_error_code_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lockstep_info *li = per_cpu_ptr(&info, dev->id);
	int ret;

	ret = lock_device_hotplug_sysfs();
	if (ret)
		goto error;

	/* Check can we use something other than stop_one_cpu()? */
	ret = stop_one_cpu(dev->id, lockstep_update_status, (void *)li);
	if (ret)
		goto exit;

	unlock_device_hotplug();

	return sysfs_emit(buf, "0x%x\n", li->break_error_code);

exit:
	unlock_device_hotplug();
error:
	return sysfs_emit(buf, "-1\n");
}
static DEVICE_ATTR_RO(break_error_code);

static struct attribute *lockstep_active_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_session.attr,
	&dev_attr_break_reason.attr,
	&dev_attr_break_error_code.attr,
	NULL
};

static const struct attribute_group lockstep_active_attr_group = {
	.name = "lockstep",
	.attrs = lockstep_active_attrs,
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

	this_cpu_write(info.session, 1);

	this_cpu_write(info.init, true);

	return 0;
}

static int lockstep_add_dev(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);
	struct lockstep_info *li = per_cpu_ptr(&info, cpu);
	int ret = 0;

	/* Check if this some of these things should be covered in the LVT interrupt upon shadow break? */
	/* If a shadow core is coming online for whatever reason, update the lockstep state */
	if (li->init && (li->role == ROLE_SHADOW) && (li->enable == 1)) {
		if (li->online_in_progress != true)
			pr_info("CPU%d Unexpected exit from lockstep\n", raw_smp_processor_id());
		li->enable = 0;
		per_cpu_ptr(&info, li->peer_cpu)->enable = 0;
		per_cpu_ptr(&info, li->peer_cpu)->session += 1;
	}

	if (!li->init) {
		ret = initialize_lockstep_info();
		if (ret)
			return ret;

		ret = sysfs_create_group(&dev->kobj, &lockstep_common_attr_group);
	}

	if (!ret && (li->role == ROLE_ACTIVE))
		ret = sysfs_merge_group(&dev->kobj, &lockstep_active_attr_group);

	return ret;
}

static int lockstep_remove_dev(unsigned int cpu)
{
	struct lockstep_info *li = per_cpu_ptr(&info, cpu);
	struct device *dev = get_cpu_device(cpu);

	/* Check if lockstep is enabled on the ACTIVE CPU. Hotplug offline should fail in that case. */
	if (li->enable)
		return -EBUSY;

	if (li->init && (li->role == ROLE_ACTIVE))
		sysfs_unmerge_group(&dev->kobj, &lockstep_active_attr_group);

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
