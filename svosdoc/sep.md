# SEP Enablement

# Description
The SVOS Next kernel has some build config settings that enable the SEP and EMON code carried from Intel Next.

SEP and EMON have a page together at https://intel.sharepoint.com/sites/performance-tools. We found it through goto.intel.com/sep.

# Change Log
May 2023: Initial work was done to add SEP permanently.

# Responsible Contacts
The main validation user that drove the addition was Chinthapally, Manisha <manisha.chinthapally@intel.com>

Adam Preble <adam.c.preble@intel.com> originally enabled it in the kernel

# Files Affected
The kernel configuration settings are either in:
1. debian/configs/svos_next_defconfig.fix
2. arch/x86/configs/svos_next_defconfig

Depending on the age of the kernel. Older kernels use defconfig.fix. The settings are then vetted and moved to
arch/x86/configs/svos_next_defconfig.

# Other Changes
The settings in SVOS come down to a special pull branch from Intel Next, that is then enabled in SVOS Next via
kernel build configuration settings added to /arch/x86/configs/svos_next_defconfig:

```
CONFIG_INTEL_SOCPERF=m
CONFIG_INTEL_SEP=y
CONFIG_SEP=m
CONFIG_SEP_SOCPERF=m
CONFIG_SEP_PAX=m
CONFIG_SEP_STANDARD_MODE=y
CONFIG_SEP_PRIVATE_BUILD=y
```

# Performance Impacts
This before/after comparison was done after-the-fact in July 2023.

| Kernel Point of Interest                          | Before            | After           | Delta % |
| ------------------------------------------------- | ----------------- | --------------- | ------- |
| initrd (bytes)                                    | 60268941          | 60277787        | 0.0%    |
| vmlinux (bytes)                                   | 8354432           | 8362592         | 0.1%    |
| /lib/modules/6.4.0.svos-next-tickless-x86-64 (MB) | 953               | 959             | 0.6%    |
| slabtop Total Size (K)                            | 30840.95K         | 55802.40K       | 80.9%   |

Memory usage considerably goes up.
