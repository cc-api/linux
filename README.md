Purpose
=======
Provide (Pre-Silicon) Best Known Configuration (BKC) kernel for Emerald Rapids
(EMR) customers.

The BKC kernel is based on v5.19-rc4. New SPR feautre patches which are not
in v5.19-rc4 are added to the BKC kernel.

WARNING this kernel contains technology preview code that is
subject to change once it goes upstream. This kernel is
strictly for hardware validation, not production. Applications
tested against this kernel may behave differently, or may not
operate at all once the code is finalized in the mainline kernel.
Use at your own risk.

Release History
===============

EMR-BKC-v1.3
12. TD migration support (Wei Wang)
13. TD guest (Kuppuswamy Sathyanarayanan)
14. one TD guest fix (Chao Gao)

EMR-BKC-v1.2
------------
10. Port a definition patch (Kan Liang)
11. Add EMR-BKC-v1 kconfig (Chenyi Qiang)

EMR-BKC-v1.1
------------
6. TD-preserving and boot-time/dynamic TDX module loading (Chao Gao)
7. CET and Arch-LBR Fix (Weijiang Yang and Chenyi Qiang)
8. Array BIST (Jithu Joseph)
9. TD-preserving Fix (Yuan Yao and Chao Gao)

EMR-BKC-v1.0
------------
1. IPIv (Guang Zeng)
2. Notify VM exit (Chenyi Qiang)
3. CET Kernel (Rick Edgecombe)
4. CET KVM (Weijiang Yang)
5. TDX Host/KVM (Isaku Yamahata)

