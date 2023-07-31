This is a template to use for documenting some category of kernel changes.
We request this is filled out because we cannot rely on git history to
establish why changes were made. Yes, commit messages can be pretty bad,
but the way SVOS Next and Intel Next code are merged together destroys a
lot of our history. So even if we had a great commit message for a change,
we'd likely have lost it.

# <Replace with Kernel Capability Term/Phrase Here>

# Description
What this change does

# How to Use
Basic information for actually using the feature. This can go to external
documentation.

# Change Log
YYYY-MM-DD: Initial Changes (we always want this to know how old this is)

YYYY-MM-DD: Something new that was done worth noting.

# Responsible Contacts
For this section, imagine who would be mad if the change was arbitrarily reverted
without notifying anybody. Then list people who should be included.

* Developers
   * First Last <first.last@intel.com>
* Maintainers, including anybody involved in SVOS core team.
   * First Last <first.last@intel.com>
* Touched As-Needed
   * First Last <first.last@intel.com>
     * Description of what and when
* Also consider mentioning teams instead of individuals that may be impacted since
  individuals can change roles or outright leave the company. Having a team name
  even if all the people in it are gone goes a long way to getting context to
  establish a new conversation about this work.

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
3. /lib/modules/x.y.0.svos-next-[config]-x86-64 size for modules matching the config built
4. Memory use reported by slabtop right after boot. If a new loadable
   kernel module is involved, try to report the before/after of with
   loading the module.

## Example
Using 6.4.0 tickless in a QEMU virtual machine:

| Kernel Point of Interest                          | Before            | After           | Delta % |
| ------------------------------------------------- | ----------------- | --------------- | ------- |
| initrd (bytes)                                    | 60158993          | 60158968        | -0.00%  |
| vmlinux (bytes)                                   | 8246688           | 8246688         | 0.0%    |
| /lib/modules/6.4.0.svos-next-tickless-x86-64 (MB) | 512               | 512             | 0.0%    |
| slabtop Total Size (K)                            | 31328.93K         | 31328.93K       | 0.0%    |

Note: The impact of this change was surprisingly small.
