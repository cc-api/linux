# Universal Flash Storage(UFS)

# Description
When the main storage device (the device that contain the OS) is UFS,
we must make sure the ufshcd driver will be loaded
during boot inorder to take control over the UFS controller and device.
The below configs cause the ufshcd driver to be loaded during the earlyboot phase.
The POR for LNL-M is to use UFS as the main storage device.

# Change Log
2023-06-08: Initial integration into the kernel.

# Responsible Contacts
Barak Feldman <barak.feldman@intel.com>
Mahendra Damahe <mahendra.damahe@intel.com>

Yonatan Amir <yonatan.amir@intel.com> was the primary contact with FV while
sorting out what was necessary.

# Files Affected
debian/configs/svos_next_defconfig.fix

# Other Changes
These kernel build flags were added to svos_next_defconfig.fix:
```
CONFIG_SCSI_UFSHCD_PCI=y
CONFIG_SCSI_UFSHCD=y
CONFIG_SCSI_UFS_BSG=y
```
# Performance Impacts
TBD: Kernel profile before after will be done by Adam Preble since he's refining
the criteria here.
