Purpose
=======
Provide (Pre-Silicon) Best Known Configuration (BKC) kernel for Emerald Rapids
(EMR) customers.

The BKC kernel is based on v5.19. New SPR feautre patches which are not
in v5.19 are added to the BKC kernel.

WARNING this kernel contains technology preview code that is
subject to change once it goes upstream. This kernel is
strictly for hardware validation, not production. Applications
tested against this kernel may behave differently, or may not
operate at all once the code is finalized in the mainline kernel.
Use at your own risk.

Release History
===============

EMR-BKC-v1.9
------------
37. one fix for Array IFS/ARRAY BIST (Jithu Joseph)

EMR-BKC-v1.8
------------
25. Some compile issue fixes (Chenyi Qiang and Wei Wang)
26. Enumerate architectual split lock (Fenghua Yu)
27. IDXD/SIOV support and some fixes (Fenghua Yu)
28. venqcmd support (Yi Sun)
29. Huge page split fix (Yuan Yao)
30. Array BIST fix (Jithu Joseph)
31. SGX AEX Notify support (Dave Hansen and Kai Huang)
32. TD preserving fix (Chao Gao)
33. TD attestation fix (Kuppuswamy Sathyanarayanan)
34. Device pass-thru fix (Chenyi Qiang)
35. Add IDXD/SIOV related and ACPI_PFRUT kconfig in EMR-BKC-v2.config (Chenyi Qiang)
36. Ucode rollback support (Ashok Raj)

EMR-BKC-v1.7
------------
22. new EMR CPU model number support for PMU (Kan Liang)
23. new EMR CPU model number support for RAPL and turbostat (Rui Zhang)
24. one fix in TDX coredump (Isaku Yamahata)

EMR-BKC-v1.6
------------
21. RAS patch fix (Fengwei Yin)

EMR-BKC-v1.5
------------
18. TD migration fix (Wei Wang)
19. Intel TH (Alexander Shishkin)
20. one Array BIST fix (Jithu Joseph)

EMR-BKC-v1.4
------------
15. TD migration fix (Wei Wang)
16. 2M page and swiotlb related fix (Yuan Yao)
17. Array BIST fix (Jithu Joseph)

EMR-BKC-v1.3
------------
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

