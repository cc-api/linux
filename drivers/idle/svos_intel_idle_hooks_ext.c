#include "svos_intel_idle_hooks_ext.h"

extern int svos_enable_intel_idle_extensions;

// Located in arch/x86/kernel/process.c:721
extern void default_idle(void);

enum svos_idle_routine_ids {
    SVOS_ORIG_IDLE, // idle routine in place when we booted
    SVOS_DEF_IDLE,          // default_idle (whatever it is)
    SVOS_MWAIT_IDLE,        // default linux mwait_idle
    SVOS_POLL_IDLE, // Traditional poll idle routine
    SVOS_ACPI_IDLE, // 2.6 compatibility
    SVOS_SVFS_POLL_IDLE,    // Custom svkernel svos_idle routine
    SVOS_HALT_IDLE,         // Kernel idle routine using hlt instruction
    SVOS_DEBUG_IDLE,        // Idle routine to verify mwait_c_state is updated in idle driver
    SVOS_NUM_IDLE           // Must terminate list, count of number of entries
};

// Table of available idle routines.
void ** svos_idle_routines[SVOS_NUM_IDLE] = {0};
EXPORT_SYMBOL(svos_idle_routines);

// If set to a non-zero, this is the index into svos_idle_routines. svkernel_svfsnode_scheduler uses custom_idle to change idle_routines.
int custom_idle = 0;
EXPORT_SYMBOL(custom_idle);

// Expose mwait_c_state to other parts of the kernel.
// svkernel_svfsnode_scheduler can write to mwait_c_state now.
int mwait_c_state = 0;
EXPORT_SYMBOL(mwait_c_state);

/** For debug purposes
    * Simple idle routine used to check if we can switch idle routines from the default idle, and if mwait_c_state is
    * changed properly
    */
static __cpuidle int svos_debug_idle(struct cpuidle_device *dev, struct cpuidle_driver *drv, int index) {

    // Could be race condition (all cpus can read and write to old_mwait_c_state), but probably does not matter.
    static int old_mwait_c_state = 0;

    if (old_mwait_c_state != mwait_c_state) {
        printk(KERN_DEBUG pr_fmt("%s, mwait_c_state has been set to as: %#08x\n"), __FUNCTION__, mwait_c_state);
        old_mwait_c_state = mwait_c_state;

        printk(KERN_DEBUG pr_fmt("Governor passed Index = %d\n"), index);
    }

    // Simply halt the cpu for now
    raw_safe_halt();
    return index;
}

/**
    * setup_svos_idle_routines_table
    *
    * initialize svos_idle_routines_table so that svkernel_svfsnode_scheduler may change idle routines
    * if requested.
    *
    * Note: It may be that svkernel_svfsnode_scheduler will assign idle routines to the table as well.
    */
static void setup_svos_idle_routines_table(void) {
    int i;

    printk(KERN_DEBUG pr_fmt("Setting up SVOS Idle routine table\n"));

    svos_idle_routines[SVOS_ORIG_IDLE]   = (void *)0;
    svos_idle_routines[SVOS_DEF_IDLE]    = (void *)&default_idle;
    svos_idle_routines[SVOS_MWAIT_IDLE]  = (void *)0; // Note: this is the intel_idle driver routine

    // poll_idle is defined as a static function in drivers/cpuidle/poll_state.c
    svos_idle_routines[SVOS_POLL_IDLE]   = (void *)kallsyms_lookup_name("poll_idle");

    // NOTE: SVOS_ACPI_IDLE will needs more setup before it can be used.
    svos_idle_routines[SVOS_ACPI_IDLE]   = (void *)0;

    //svos_idle_routines[SVOS_POLL_IDLE]   = (void *)0; // Note: this will be setup by svkernel_svfsnode_scheduler
    svos_idle_routines[SVOS_HALT_IDLE]   = (void *)&default_idle; //Note: default_idle uses the halt instruction.

    svos_idle_routines[SVOS_DEBUG_IDLE]  = (void *)svos_debug_idle;

    printk(KERN_DEBUG pr_fmt("svos_idle_routines = %p\n"), svos_idle_routines);

    for (i = 0; i < SVOS_NUM_IDLE; i++) {
        printk(KERN_DEBUG pr_fmt("[%d] i=%d:functionPointer=%p\n"), __LINE__, i, svos_idle_routines[i]);
    }

    return;
}

/**
    * Enable c1e promotion if enable != 0, otherwise disable.
    */
void __set_c1e_promotion(void * enable) {
    if ( *((int *) enable) ) {
        c1e_promotion_enable();
    } else {
        c1e_promotion_disable();
    }
}

/**
    * sets the c1e prometion bit in IA32_POWER_CTL MSR.
    * @enable: if enable == 1: allow for c1e cstate promotion, otherwise: only c1 cstate may be entered.
    *
    * C1 and C1E are mutually exclusive cstates. The actual cstate the cpu enters is determined by the IA32_POWER_CTL MSR.
    */
void set_c1e_promotion(int enable) {
    // wait for all cpus to disable or enable c1e promotion.
    // NOTE: This is not efficient as IA32_POWER_CTL Affects the whole cpu package, so running this on every cpu is overkill.
    //       Running once on each package would have the same affect.
    if (enable) {
        printk(KERN_DEBUG pr_fmt("c1e promotion enabled\n"));
    } else {
        printk(KERN_DEBUG pr_fmt("c1e promotion disabled\n"));
    }
    on_each_cpu(__set_c1e_promotion, (void *) &enable, 1);
}
EXPORT_SYMBOL(set_c1e_promotion);

int is_c1e_promotion_enabled(void) {
    int enabled;
    unsigned long long msr_bits;

    rdmsrl(MSR_IA32_POWER_CTL, msr_bits);
    enabled = (msr_bits & 0x2) >> 1;

    return enabled;
}
EXPORT_SYMBOL(is_c1e_promotion_enabled);

/**
 * Resets this idle driver SVFS parameters to default values. Idle driver will behave like default/generic intel_idle.
 */
void reset_intel_idle_driver(void) {
    printk(KERN_DEBUG pr_fmt("Reseting intel_idle driver SVOS Scheduler parameters...\n"));

    custom_idle = 0;
    mwait_c_state = 0;
    if (c1e_promotion == C1E_PROMOTION_ENABLE) {
        set_c1e_promotion(1);
    } else {
        set_c1e_promotion(0);
    }

}
EXPORT_SYMBOL(reset_intel_idle_driver);
