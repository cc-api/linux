/* SPDX-License-Identifier: GPL-2.0 */
/* helper functions to invoke SEAM ACM. */

#ifndef _X86_TDX_SEAM_H
#define _X86_TDX_SEAM_H

#include <linux/earlycpio.h>

struct vmcs_hdr {
	u32 revision_id:31;
	u32 shadow_vmcs:1;
};

struct vmcs {
	struct vmcs_hdr hdr;
	u32 abort;
	char data[];
};

/*
 * cpu_vmxon() - Enable VMX on the current CPU
 *
 * Set CR4.VMXE and enable VMX
 */
static inline int cpu_vmxon(u64 vmxon_pointer)
{
	u64 msr;

	cr4_set_bits(X86_CR4_VMXE);

	asm_volatile_goto("1: vmxon %[vmxon_pointer]\n\t"
			_ASM_EXTABLE(1b, %l[fault])
			: : [vmxon_pointer] "m"(vmxon_pointer)
			: : fault);
	return 0;

fault:
	WARN_ONCE(1, "VMXON faulted, MSR_IA32_FEAT_CTL (0x3a) = 0x%llx\n",
		rdmsrl_safe(MSR_IA32_FEAT_CTL, &msr) ? 0xdeadbeef : msr);
	cr4_clear_bits(X86_CR4_VMXE);

	return -EFAULT;
}

bool __init seam_get_firmware(struct cpio_data *blob, const char *name);

bool __init is_seamrr_enabled(void);

int __init __seam_init_vmx_early(void);
int __init seam_init_vmx_early(void);
void __init seam_init_vmxon_vmcs(struct vmcs *vmcs);

void __init seam_free_vmcs_tmp_set(void);
int __init seam_alloc_init_vmcs_tmp_set(void);
int __init seam_vmxon_on_each_cpu(void);
int __init seam_vmxoff_on_each_cpu(void);

#endif /* _X86_TDX_SEAM_H */
