An official standard for general code changes in SVOS Next still hasn't been
established so this is still rather eclectic, in fact, we forget to even write
6.5 notes.

# svoskern apic functions

We noticed in svfs that we were getting modpost errors for some apic functions
when we built it against 6.6. These functions do get used in svfs:

* apic_eoi
* apic_read
* apic_write
* apic_icr_read
* apic_icr_write
* apic_wait_icr_idle

What happened in 6.6 is that they were marked as static_call() which hides them
in the kernel. 

We created svoskern_* versions of this functions in our kernel, specifically in
svos.c where they are protected by the CONFIG_SVOS build config option. They
are exported as symbols we can use in svfs. Svfs has a matching modification to
call the new svoskern_* versions.

There is also read_apic_id is not a direct static_call, but it uses apic_read,
and is invoked as an inline function in apic.h in svfs. So we had to guard that
call to use the svoskern version as well.

## Some Modification Details
commit 25388921e8ab2e01f9a47a6516bb1bf6f5a5b11e

* /arch/x86/include/asm/apic.h
   * Added CONFIG_SVOS guard to use svoskern_apic_read in read_apic_id
   * It is extern because svoskern is defined in svos.c
* arch/x86/kernel/svos.c
   * Add #include for apic.h
   * Added svoskern apic wrappers
* include/linux/svos.h
   * Added extern definitions for svoskern apic wrappers

# Removed KERNEL_VERSION checks in telemetry examples

Telemetry was added in 6.3, and every kernel we've made has since supported it.
Since we have just deprecated 6.3, we no longer have to test for it or older
kernels at all.

## Some Modification Details
commit c353432fc93a733a5728ed279cd84e1c2686143d
All changes are in the example documentation in include/linux/telemetry/native.h

# Added prototypes for some kdba functions

Intel Next's kernel test robot likes to complain about these old SVOS Next kdb
override functions:

* kdba_id_parsemode
* kdba_check_pc
* kdba_id_printinsn
* kdba_id_init

It was using a GCC 13 version that required a glibc we didn't have available
to us anywhere . . . until Bookworm was released. So we took a stab at it this
time.

We created a kdba_id.h file to declare prototypes for them. We included the
typedef for kdb_machreg_t as well in the header file.

## Some Modification Details
commit 6cd40b0ff55cd8416888f8416f3d57fa244e0362

* Created kernel/debug/kdb/svkdb/kdba_id.h and defined missing prototypes inside
  of it
* Modified kernel/debug/kdb/svkdb/kdba_id.c to use the header

# Add GNU printf attribute to kdb_dis_fprintf
    
The GCC 13 test build from Intel Next further flagged kdb_dis_fprintf for not
having the printf attribute. This was a warning, which then became an error.

This is probably overkill for error checking but we're fixing it anyways. The
attribute marks the function as having printf-like capabilities and will enable
additional checks on usages of it to ensure formatting is correct to a printf-
like function. That didn't sound like a bad thing.

## Some Modification Details
commit 385b8b712f650fe572538a059eee7f3c73121b61

Modified kernel/debug/kdb/svkdb/kdb_id.c to add the gnu_printf attribute.
