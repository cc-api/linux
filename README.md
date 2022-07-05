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

EMR-BKC-v1.0
------------
1. IPIv (Guang Zeng)
2. Notify VM exit (Chenyi Qiang)
3. CET Kernel (Rick Edgecombe)
4. CET KVM (Weijiang Yang)
5. TDX Host/KVM (Isaku Yamahata)

