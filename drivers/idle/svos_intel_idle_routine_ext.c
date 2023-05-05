void (* idle_p)(struct cpuidle_device *, struct cpuidle_driver *, int);

if (svos_enable_intel_idle_extensions) {
    /*
        *  Try using specified idle routine if it exists:
        *
        *  custom_idle == 0               : use registered idle routine which is this idle routine: intel_idle(...).
        *  custom_idle == SVOS_MWAIT_IDLE : use mwait_idle routine which is also intel_idle(...)
        */
    if (custom_idle && (custom_idle != SVOS_MWAIT_IDLE) && svos_idle_routines[custom_idle]) {
        idle_p = (void *)svos_idle_routines[custom_idle];
        idle_p(dev, drv, index);
        return index;
    }

    /*
        *  Use mwait_c_state if specified.
        */
    if (mwait_c_state) {
        eax = mwait_c_state;
    }
}