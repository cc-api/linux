/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021-2022 Intel Corporation */
#ifndef _ASM_X86_TDX_H
#define _ASM_X86_TDX_H

#include <linux/init.h>
#include <linux/bits.h>

#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/shared/tdx.h>

#include <asm/trapnr.h>

/*
 * SW-defined error codes.
 *
 * Bits 47:40 == 0xFF indicate Reserved status code class that never used by
 * TDX module.
 */
#define TDX_ERROR_BIT			63
#define TDX_ERROR			_BITULL(TDX_ERROR_BIT)
#define TDX_SW_ERROR			(TDX_ERROR | GENMASK_ULL(47, 40))
#define TDX_SEAMCALL_VMFAILINVALID	(TDX_SW_ERROR | _ULL(0xFFFF0000))

#define TDX_SEAMCALL_GP			(TDX_SW_ERROR | X86_TRAP_GP)
#define TDX_SEAMCALL_UD			(TDX_SW_ERROR | X86_TRAP_UD)

#define TDX_NON_RECOVERABLE_BIT		62
/*
 * Error with the non-recoverable bit cleared indicates that the error is
 * likely recoverable (e.g. due to lock busy in TDX module), and the seamcall
 * can be retried.
 */
#define TDX_SEAMCALL_ERR_RECOVERABLE(err) \
	(err >> TDX_NON_RECOVERABLE_BIT == 0x2)

/* The max number of seamcall retries */
#define TDX_SEAMCALL_RETRY_MAX	10000

#ifndef __ASSEMBLY__

/* TDX supported page sizes from the TDX module ABI. */
#define TDX_PS_4K	0
#define TDX_PS_2M	1
#define TDX_PS_1G	2
#define TDX_PS_NR	(TDX_PS_1G + 1)

/*
 * Used by the #VE exception handler to gather the #VE exception
 * info from the TDX module. This is a software only structure
 * and not part of the TDX module/VMM ABI.
 */
struct ve_info {
	u64 exit_reason;
	u64 exit_qual;
	/* Guest Linear (virtual) Address */
	u64 gla;
	/* Guest Physical Address */
	u64 gpa;
	u32 instr_len;
	u32 instr_info;
};

#ifdef CONFIG_INTEL_TDX_GUEST

extern int tdx_notify_irq;

void __init tdx_early_init(void);
bool tdx_debug_enabled(void);

void tdx_get_ve_info(struct ve_info *ve);

void __init tdx_filter_init(void);

bool tdx_handle_virt_exception(struct pt_regs *regs, struct ve_info *ve);

void tdx_safe_halt(void);

bool tdx_early_handle_ve(struct pt_regs *regs);

int tdx_mcall_get_report0(u8 *reportdata, u8 *tdreport);

bool tdx_allowed_port(int port);

u64 tdx_mcall_verify_report(u8 *reportmac);

int tdx_mcall_extend_rtmr(u8 *data, u8 index);

int tdx_hcall_get_quote(void *tdquote, int size);

#else

static inline void tdx_early_init(void) { };
static inline void tdx_safe_halt(void) { };
static inline void tdx_filter_init(void) { };

static inline bool tdx_early_handle_ve(struct pt_regs *regs) { return false; }

#endif /* CONFIG_INTEL_TDX_GUEST */

#if defined(CONFIG_KVM_GUEST) && defined(CONFIG_INTEL_TDX_GUEST)
long tdx_kvm_hypercall(unsigned int nr, unsigned long p1, unsigned long p2,
		       unsigned long p3, unsigned long p4);
#else
static inline long tdx_kvm_hypercall(unsigned int nr, unsigned long p1,
				     unsigned long p2, unsigned long p3,
				     unsigned long p4)
{
	return -ENODEV;
}
#endif /* CONFIG_INTEL_TDX_GUEST && CONFIG_KVM_GUEST */

#ifdef CONFIG_INTEL_TDX_HOST

/* -1 indicates CPUID leaf with no sub-leaves. */
#define TDX_CPUID_NO_SUBLEAF	((u32)-1)
struct tdx_cpuid_config {
	__struct_group(tdx_cpuid_config_leaf, leaf_sub_leaf, __packed,
		u32 leaf;
		u32 sub_leaf;
	);
	__struct_group(tdx_cpuid_config_value, value, __packed,
		u32 eax;
		u32 ebx;
		u32 ecx;
		u32 edx;
	);
} __packed;

#define TDSYSINFO_STRUCT_SIZE		1024

/*
 * The size of this structure itself is flexible.  The actual structure
 * passed to TDH.SYS.INFO must be padded to 1024 bytes and be 1204-bytes
 * aligned.
 */
struct tdsysinfo_struct {
	/* TDX-SEAM Module Info */
	u32	attributes;
	u32	vendor_id;
	u32	build_date;
	u16	build_num;
	u16	minor_version;
	u16	major_version;
	u8	sys_rd;
	u8	reserved0[13];
	/* Memory Info */
	u16	max_tdmrs;
	u16	max_reserved_per_tdmr;
	u16	pamt_entry_size;
	u8	reserved1[10];
	/* Control Struct Info */
	u16	tdcs_base_size;
	u8	reserved2[2];
	u16	tdvps_base_size;
	u8	tdvps_xfam_dependent_size;
	u8	reserved3[9];
	/* TD Capabilities */
	u64	attributes_fixed0;
	u64	attributes_fixed1;
	u64	xfam_fixed0;
	u64	xfam_fixed1;
	u8	reserved4[32];
	u32	num_cpuid_config;
	/*
	 * The actual number of CPUID_CONFIG depends on above
	 * 'num_cpuid_config'.
	 */
	DECLARE_FLEX_ARRAY(struct tdx_cpuid_config, cpuid_configs);
} __packed;

#include <linux/bug.h>
static __always_inline int pg_level_to_tdx_sept_level(enum pg_level level)
{
	WARN_ON_ONCE(level == PG_LEVEL_NONE);
	return level - 1;
}

#include <asm/processor.h>
static __always_inline u64 set_hkid_to_hpa(u64 pa, u16 hkid)
{
	return pa | ((u64)hkid << boot_cpu_data.x86_phys_bits);
}

const struct tdsysinfo_struct *tdx_get_sysinfo(void);
bool platform_tdx_enabled(void);
int tdx_cpu_enable(void);
int tdx_enable(void);
void tdx_reset_memory(void);
bool tdx_is_private_mem(unsigned long phys);

/*
 * Key id globally used by TDX module: TDX module maps TDR with this TDX global
 * key id.  TDR includes key id assigned to the TD.  Then TDX module maps other
 * TD-related pages with the assigned key id.  TDR requires this TDX global key
 * id for cache flush unlike other TD-related pages.
 */
extern u32 tdx_global_keyid __ro_after_init;
u32 tdx_get_nr_guest_keyids(void);
int tdx_guest_keyid_alloc(void);
void tdx_guest_keyid_free(int keyid);

u64 __seamcall(u64 op, u64 rcx, u64 rdx, u64 r8, u64 r9, u64 r10,
	       u64 r11, u64 r12, u64 r13,
	       u64 r14, u64 r15, struct tdx_module_output *out);

#define DEBUGCONFIG_TRACE_ALL		0
#define DEBUGCONFIG_TRACE_WARN		1
#define DEBUGCONFIG_TRACE_ERROR		2
#define DEBUGCONFIG_TRACE_CUSTOM	1000
#define DEBUGCONFIG_TRACE_NONE		-1ULL
void tdx_trace_seamcalls(u64 level);

/* tdxio related */
#include <linux/percpu.h>
struct vmx_tdx_enabled {
	cpumask_var_t vmx_enabled;
	atomic_t err;
};

int vmxon_all(struct vmx_tdx_enabled *vmx_tdx);
void vmxoff_all(struct vmx_tdx_enabled *vmx_tdx);
bool tdx_io_support(void);
void tdx_clear_page(unsigned long page_pa, int size);
int tdx_reclaim_page(unsigned long pa, enum pg_level level, bool do_wb, u16 hkid);
void tdx_reclaim_td_page(unsigned long td_page_pa);

/* Temp solution, copied from tdx_error.h */
#define TDX_INTERRUPTED_RESUMABLE		0x8000000300000000ULL
#define TDX_VCPU_ASSOCIATED			0x8000070100000000ULL
#define TDX_VCPU_NOT_ASSOCIATED			0x8000070200000000ULL

static inline u64 seamcall_retry(u64 op, u64 rcx, u64 rdx, u64 r8, u64 r9,
			         u64 r10, u64 r11, u64 r12, u64 r13,
			         u64 r14, u64 r15,
			         struct tdx_module_output *out)
{
	u64 ret, retries = 0;

	do {
		ret = __seamcall(op, rcx, rdx, r8, r9,
				 r10, r11, r12, r13, r14, r15, out);
		if (unlikely(ret == TDX_SEAMCALL_UD)) {
			/*
			 * SEAMCALLs fail with TDX_SEAMCALL_UD returned when
			 * VMX is off. This can happen when the host gets
			 * rebooted or live updated. In this case, the
			 * instruction execution is ignored as KVM is shut
			 * down, so the error code is suppressed. Other than
			 * this, the error is unexpected and the execution
			 * can't continue as the TDX features reply on VMX to
			 * be on.
			 */
			pr_err("%s ret 0x%llx TDX_SEAMCALL_UD\n", __func__, ret);
			return ret;
		}

		/*
		 * On success, non-recoverable errors, or recoverable errors
		 * that don't expect retries, hand it over to the caller.
		 */
		if (!ret ||
		    ret == TDX_VCPU_ASSOCIATED ||
		    ret == TDX_VCPU_NOT_ASSOCIATED ||
		    ret == TDX_INTERRUPTED_RESUMABLE)
			return ret;

		if (retries++ > TDX_SEAMCALL_RETRY_MAX)
			break;
	} while (TDX_SEAMCALL_ERR_RECOVERABLE(ret));

	return ret;
}

#define TDH_PHYMEM_PAGE_RECLAIM		28
#define TDH_PHYMEM_PAGE_WBINVD		41

static inline u64 tdh_phymem_page_reclaim(u64 page,
					  struct tdx_module_output *out)
{
	return seamcall_retry(TDH_PHYMEM_PAGE_RECLAIM,
			      page, 0, 0, 0, 0, 0, 0, 0, 0, 0, out);
}

static inline u64 tdh_phymem_page_wbinvd(u64 page)
{
	return seamcall_retry(TDH_PHYMEM_PAGE_WBINVD,
			      page, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL);
}

#else	/* !CONFIG_INTEL_TDX_HOST */
struct tdsysinfo_struct;
static inline const struct tdsysinfo_struct *tdx_get_sysinfo(void) { return NULL; }
static inline bool platform_tdx_enabled(void) { return false; }
static inline int tdx_cpu_enable(void) { return -ENODEV; }
static inline int tdx_enable(void)  { return -ENODEV; }
static inline void tdx_reset_memory(void) { }
static inline bool tdx_is_private_mem(unsigned long phys) { return false; }
static inline u64 __seamcall(u64 op, u64 rcx, u64 rdx, u64 r8, u64 r9,
			     u64 r10, u64 r11, u64 r12, u64 r13,
			     u64 r14, u64 r15,
			     struct tdx_module_output *out)
{
	return TDX_SEAMCALL_UD;
}
static inline u32 tdx_get_nr_guest_keyids(void) { return 0; }
static inline int tdx_guest_keyid_alloc(void) { return -EOPNOTSUPP; }
static inline void tdx_guest_keyid_free(int keyid) { }

/* tdxio related */
struct vmx_tdx_enabled;
static inline int vmxon_all(struct vmx_tdx_enabled *vmx_tdx) { return -EOPNOTSUPP; }
static inline void vmxoff_all(struct vmx_tdx_enabled *vmx_tdx) {}
static inline bool tdx_io_support(void) { return false; }
static inline u64 seamcall_retry(u64 op, u64 rcx, u64 rdx, u64 r8, u64 r9,
			         u64 r10, u64 r11, u64 r12, u64 r13,
			         u64 r14, u64 r15,
			         struct tdx_module_output *out)
{
	return TDX_SEAMCALL_UD;
}
static inline void tdx_clear_page(unsigned long page_pa, int size) { }
static inline int tdx_reclaim_page(unsigned long pa, enum pg_level level, bool do_wb,
				   u16 hkid) { return -EOPNOTSUPP; }
static inline void tdx_reclaim_td_page(unsigned long td_page_pa) { }
static inline u64 tdh_phymem_page_reclaim(u64 page,
					  struct tdx_module_output *out) { return -EOPNOTSUPP; }
static inline u64 tdh_phymem_page_wbinvd(u64 page) { return -EOPNOTSUPP; }

#endif	/* CONFIG_INTEL_TDX_HOST */

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_TDX_H */
