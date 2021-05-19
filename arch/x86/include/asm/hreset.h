/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_HRESET_H

/* Find the supported HRESET features in this leaf */
#define CPUID_HRESET_LEAF_EAX		0x20

#ifdef __ASSEMBLY__

/**
 * HRESET - History reset. Available since binutils vX.YY
 *
 * Provides a hint to reset a subset of features of the history prediction in
 * current processor. The subset of features is indicated in %eax. The
 * instruction defines an 1-byte immediate operand, which is ignored. The
 * assembly code would look like:
 *
 *	hreset %eax, $0
 *
 * The corresponding machine code looks like:
 *
 *	F3 0F 3A F0 ModRM Imm
 *
 * F3 is a mandatory prefix (?). Thee ModRM must specify register addressing
 * and use the %eax register. The machine code below uses 0xc0 for such
 * purpose. The ignored immediate operand is set as 0.
 *
 * The instruction is documented in the Intel Architecture Instruction Set
 * Extensions and Future Programming Reference.
 */

#define __ASM_HRESET  .byte 0xf3, 0xf, 0x3a, 0xf0, 0xc0, 0x0

#else /* __ASSEMBLY */

#ifdef __KERNEL__
extern void hreset_reload(void);
#endif /* __KERNEL__ */

#endif /* __ASSEMBLY */

#endif /* _ASM_X86_HRESET_H */
