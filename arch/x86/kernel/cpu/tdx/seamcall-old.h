/* SPDX-License-Identifier: GPL-2.0-only */
/* C function wrapper for SEAMCALL */
#ifndef __SEAM_SEAMCALL_H
#define __SEAM_SEAMCALL_H

#include <linux/linkage.h>

#include <asm/seam.h>

#ifdef CONFIG_INTEL_TDX_HOST
/*
 * TDX extended return:
 * Some of The "TDX module" SEAMCALLs return extended values (which are function
 * leaf specific) in registers in addition to the completion status code in
 * %rax.  For example, in the error case of TDH.SYS.INIT, the registers hold
 * more detailed information about the error in addition to an error code.  Note
 * that some registers may be unused depending on SEAMCALL functions.
 */
struct tdx_ex_ret {
	union {
		struct {
			u64 rcx;
			u64 rdx;
			u64 r8;
			u64 r9;
			u64 r10;
			u64 r11;
		} regs;
		/*
		 * TDH_SYS_INFO returns the buffer address and its size, and the
		 * CMR_INFO address and its number of entries.
		 */
		struct {
			u64 buffer;
			u64 nr_bytes;
			u64 cmr_info;
			u64 nr_cmr_entries;
		} sys_info;
		/* TDH_SYS_TDMR_INIT returns the input PA and next PA. */
		struct {
			u64 prev;
			u64 next;
		} sys_tdmr_init;
	};
};

static inline u64 seamcall_old(u64 op, u64 rcx, u64 rdx, u64 r8, u64 r9,
			struct tdx_ex_ret *ex)
{
	u64 err;
	struct seamcall_regs_in in;
	struct seamcall_regs_out out;

	in = (struct seamcall_regs_in){
		.rcx = rcx,
		.rdx = rdx,
		.r8 = r8,
		.r9 = r9,
	};
	err = __seamcall(op, &in, &out);
	if (ex) {
		*ex = (struct tdx_ex_ret) {
			.regs = {
				.rcx = out.rcx,
				.rdx = out.rdx,
				.r8 = out.r8,
				.r9 = out.r9,
				.r10 = out.r10,
				.r11 = out.r11,
			}
		};
	}

	return err;
}

#endif

#endif /* __SEAM_SEAMCALL_H */
