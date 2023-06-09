# <Replace with Kernel Capability Term/Phrase Here>

# Description
What this change does

# How to Use
Basic information for actually using the feature. This can go to external
documentation.

# Change Log
YYYY-MM-DD: Initial Changes (we always want this to know how old this is)

YYYY-MM-DD: Something that was done worth noting.

# Responsible Contacts
* Developers
   * First Last <first.last@intel.com>
* Maintainers, including anybody involved in SVOS core team.
   * First Last <first.last@intel.com>
* Touched As-Needed
   * First Last <first.last@intel.com>
     * Description of what and when

# Files Affected
Though it doesn't have to be comprehensive, try your best.

* foo/bar.c
* include/linux/foo.h

# Other Changes
Things like kernel build flags, constants, Non-source file tweaks.

# Performance Impacts
If applicable. This is more for kernel flag changes and things like that.
The current BKM is to build the kernel with and without this new
change and then record the delta in a VM between:

1. vmlinux size
2. initrd size
3. /etc/modules/x size for modules matching the config built
4. Memory use reported by slabtop right after boot. If a new loadable
   kernel module is involved, try to report the before/after of with
   loading the module.
