# Add PCIE DPC/EDR to SVOS Kernels

# Description
What this change does
We added PCIE parameters to svos-next kernel config (svos_next_defconfig.fix) in order to clean ptlpe errors and be able to run posion stress tests needed
for RAS content execution:

CONFIG_PCIE_DPC=y 
CONFIG_PCIE_EDR=y
CONFIG_PCIEAER=y

# How to Use
Enable EdpcEn BIOS Knob to check Fatal Errors (0x1) or also Non-Fatal Errors (0x2).

# Change Log
2023-08-01: be4613eec98be1c62686f8f146b2648183228590

# Responsible Contacts
* Developers
   * Ricardo Delsordo ricardo.delsordo.bustillo@intel.com - IVE XPV FV RAS Team
   * Isai Herrera isai.herrera.leandro@intel.com - IVE XPV FV RAS Team
   * Adam Preble C adam.c.preble@intel.com - Global OS AN Team

# Files Affected
* /debian/configs/svos_next_defconfig.fix

# Other Changes
N/A

# Performance Impacts
Using 6.4.0 default (6.4.0_svos-next-default_EMR_ras) in a EMR A0 system machine (AN04WVAW4251):

| Kernel Point of Interest                          | Before            | After           | Delta % |
| ------------------------------------------------- | ----------------- | --------------- | ------- |
| initrd (bytes)                                    | 64759143          | 64778459        | 0.03%   |
| vmlinuz (bytes)                                   | 8318336           | 8318336         | 0.00%   |
| /lib/modules/6.4.0.svos-next-default-x86-64 (MB)  | 542               | 542             | 0.00%   |
| slabtop Total Size (K)                            | 309389.42K        | 309087.08K      | -0.10%  |
