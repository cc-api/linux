# Kernel Telemetry

# Change Log
2023-04-26: Added Documentation to feature in advance of 6.3 kernel release where the
feature was added.

# Description
These changes enable SVOS kernel telemetry via:

1. Empty function blocks that kernel level code would call to emit a telemetry
   event. These would be called from the kernel itself, or kernel modules, or
   any code that runs in kernel space like drivers.
2. Enabling the extended Berkeley Packet Filter (eBPF) kernel feature. This
   allows a program to be run in a kernel-space bytecode VM to ferry the
   parameters of those empty function blocks into user space. From there,
   a host application can collect and upload the information without risking
   the kernel.

The host application is available via the kernel-telemetry-host package. Note,
naming standards may change it to svos-kernel-telemetry-host at some point.

# Where to Use

These are the places where this capability may be relevant to you:
1. The kernel itself
2. Svfs or svdefs code that runs in kernel modules
3. Other kernel modules
4. Drivers
5. If doubt, if you can `#include <linux/telemetry/native.h>` then this is
   relevant to you!
   
You should consider telemetry messages inside the kernel for things like:
1. Reporting using new features.
2. Running dead code.
3. Getting information about some values you want across your user base that
   you can't otherwise easily extract.
   
The [master telemetry document](https://github.com/intel-restricted/libraries.validation.system-validation.svos-libs/blob/master/observability/telemetry.md) gives some more, general advice.

# How to Use
All calls are included in include/linux/telemetry/native.h and should naturally
link against the kernel. You need a database name a telemetry ID registered with
Lantern Rock because these will be passed through the function calls. The header
file has javadoc-style documentation that can be munged by doxygen.

It would help to look at our overall document about telemetry for details:
https://github.com/intel-restricted/libraries.validation.system-validation.svos-libs/blob/master/observability/telemetry.md

A rough summary of how to literally use in code:
```c
#include <linux/telemetry/native.h>

#define SANDBOX_ID "00000000-1111-2222-3333-444444444444"

int hello = 0;
register_telemetry(SANDBOX_ID, "hello", "1.0.0");
session_begin(SANDBOX_ID, "startup");

{
   // Recommend putting buffers in a closure so they only take up space for as long as they are in use.
   char telemetry_buffer[256];
 
   snprintf(telemetry_buffer, 256, "{ hello_world: %s }",
            hello == 1 ? "true" : "false");
   telemetry_msg(SANDBOX_ID, "loaded", telemetry_buffer);
}
 
session_end(SANDBOX_ID);
unregister_telemetry(SANDBOX_ID);
```

# Responsible Contacts
* Developers
  * Adam Preble <adam.c.preble@intel.com>

# Files Affected

* telemetry/native.c
* telemetry/Makefile
* include/linux/telemetry/native.h
* include/linux/telemetry/version.h
* debian/configs/svos_next_defconfig.fix

# Other Changes
These kernel build flags were added to /arch/x86/configs/svos_next_defconfig:
```
CONFIG_NET_ACT_BPF=m
CONFIG_BPF_JIT=y
CONFIG_BPF_EVENTS=y
CONFIG_DEBUG_INFO_NONE=n
CONFIG_DEBUG_INFO_DWARF4=y
CONFIG_DEBUG_INFO_BTF=y
```

These enable some other flags as a consequence. They aren't itemized here, but
some examples:

```
CONFIG_BPF_SYSCALL=y
CONFIG_NET_CLS_BPF=m
```

# Performance Impacts

Actually using the feature is not very expensive. The function calls are
literally empty. The eBPF virtual machine is using a kprobe to extract the
values. This adds a breakpoint interrupt to each of these functions that
will break into the eBPF virtual machine. This then runs a small program
to copy all the parameters into user memory.

Generally, you would want to treat the telemetry calls like you're executing
a printk. They can be chatty in bursts but putting them together too tightly
can cause problems. The type of data you emit should be pretty short, but
should still be formatted as a JSON object. Think a dictionary inside
curly brackets.

Assume for Lantern Rock that a medium database load is around 10MB of traffic
per day.

In a VM, slabtop measured the tickless kernel using 31-32MB when I booted into a
kernel with the feature enabled. This profile stays pretty constant while the
eBPF program is running, after it is exited, and after it is finally unloaded.
The disk images themselves do increase more noticably:
 
(bytes)
tickless vmlinuz: 7,955,872 -> 9,520,832 (+ 1,564,960, ~20% increase)
tickless initrd: 22,157,702 -> 66,303,747 (+ 44,146,045, ~300% increase)
