/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _X86_MICROCODE_INTERNAL_H
#define _X86_MICROCODE_INTERNAL_H

#include <linux/earlycpio.h>
#include <linux/initrd.h>

#include <asm/cpu.h>
#include <asm/microcode.h>

struct ucode_patch {
	struct list_head plist;
	void *data;		/* Intel uses only this one */
	unsigned int size;
	u32 patch_id;
	u16 equiv_cpu;
};

extern struct list_head microcode_cache;

struct device;

enum _late_load_flags {
	__LATE_LOAD_BOTH,
	__LATE_LOAD_SAFE,
	__LATE_LOAD_MAX,
};

enum late_load_flags {
	LATE_LOAD_BOTH = BIT(__LATE_LOAD_BOTH),
	LATE_LOAD_SAFE = BIT(__LATE_LOAD_SAFE),
	LATE_LOAD_MAX  = BIT(__LATE_LOAD_MAX)
};

enum ucode_state {
	UCODE_OK	= 0,
	UCODE_NEW,
	UCODE_UPDATED,
	UCODE_NFOUND,
	UCODE_UPDATED_PART,
	UCODE_UPDATED_AUTH,
	UCODE_ERROR,
};

enum ucode_load_scope {
	NO_LATE_UPDATE = 0,
	CORE_SCOPE,
	PACKAGE_SCOPE,
	PLATFORM_SCOPE,
};

enum reload_type {
	RELOAD_COMMIT,
	RELOAD_NO_COMMIT,
	RELOAD_ROLLBACK,
	RELOAD_INVALID,
};

struct microcode_ops {
	enum late_load_flags (*get_control_flags)(void);
	enum ucode_load_scope (*get_load_scope)(void);
	enum ucode_state (*request_microcode_fw)(int cpu, struct device *device,
						 enum reload_type type);
	bool (*check_pending_commits)(void);
	int (*perform_commit)(void);
	bool (*is_rollback_supported)(void);
	void (*microcode_fini_cpu)(int cpu);
	int (*pre_apply)(enum reload_type type);
	void (*post_apply)(enum reload_type type, bool success);

	/*
	 * The generic 'microcode_core' part guarantees that
	 * the callbacks below run on a target cpu when they
	 * are being called.
	 * See also the "Synchronization" section in microcode_core.c.
	 */
	enum ucode_state (*apply_microcode)(int cpu, enum reload_type type);
	int (*collect_cpu_info)(int cpu, struct cpu_signature *csig);
	u32 (*get_current_rev)(void);
};

extern struct ucode_cpu_info ucode_cpu_info[];
struct cpio_data find_microcode_in_initrd(const char *path, bool use_pa);

#define MAX_UCODE_COUNT 128

#define QCHAR(a, b, c, d) ((a) + ((b) << 8) + ((c) << 16) + ((d) << 24))
#define CPUID_INTEL1 QCHAR('G', 'e', 'n', 'u')
#define CPUID_INTEL2 QCHAR('i', 'n', 'e', 'I')
#define CPUID_INTEL3 QCHAR('n', 't', 'e', 'l')
#define CPUID_AMD1 QCHAR('A', 'u', 't', 'h')
#define CPUID_AMD2 QCHAR('e', 'n', 't', 'i')
#define CPUID_AMD3 QCHAR('c', 'A', 'M', 'D')

#define CPUID_IS(a, b, c, ebx, ecx, edx)	\
		(!(((ebx) ^ (a)) | ((edx) ^ (b)) | ((ecx) ^ (c))))

/*
 * In early loading microcode phase on BSP, boot_cpu_data is not set up yet.
 * x86_cpuid_vendor() gets vendor id for BSP.
 *
 * In 32 bit AP case, accessing boot_cpu_data needs linear address. To simplify
 * coding, we still use x86_cpuid_vendor() to get vendor id for AP.
 *
 * x86_cpuid_vendor() gets vendor information directly from CPUID.
 */
static inline int x86_cpuid_vendor(void)
{
	u32 eax = 0x00000000;
	u32 ebx, ecx = 0, edx;

	native_cpuid(&eax, &ebx, &ecx, &edx);

	if (CPUID_IS(CPUID_INTEL1, CPUID_INTEL2, CPUID_INTEL3, ebx, ecx, edx))
		return X86_VENDOR_INTEL;

	if (CPUID_IS(CPUID_AMD1, CPUID_AMD2, CPUID_AMD3, ebx, ecx, edx))
		return X86_VENDOR_AMD;

	return X86_VENDOR_UNKNOWN;
}

static inline unsigned int x86_cpuid_family(void)
{
	u32 eax = 0x00000001;
	u32 ebx, ecx = 0, edx;

	native_cpuid(&eax, &ebx, &ecx, &edx);

	return x86_family(eax);
}

extern bool initrd_gone;

#ifdef CONFIG_CPU_SUP_AMD
void load_ucode_amd_bsp(unsigned int family);
void load_ucode_amd_ap(unsigned int family);
void load_ucode_amd_early(unsigned int cpuid_1_eax);
int save_microcode_in_initrd_amd(unsigned int family);
void reload_ucode_amd(unsigned int cpu);
struct microcode_ops *init_amd_microcode(void);
void exit_amd_microcode(void);
#else /* CONFIG_CPU_SUP_AMD */
static inline void load_ucode_amd_bsp(unsigned int family) { }
static inline void load_ucode_amd_ap(unsigned int family) { }
static inline void load_ucode_amd_early(unsigned int family) { }
static inline int save_microcode_in_initrd_amd(unsigned int family) { return -EINVAL; }
static inline void reload_ucode_amd(unsigned int cpu) { }
static inline struct microcode_ops *init_amd_microcode(void) { return NULL; }
static inline void exit_amd_microcode(void) { }
#endif /* !CONFIG_CPU_SUP_AMD */

#ifdef CONFIG_CPU_SUP_INTEL
void load_ucode_intel_bsp(void);
void load_ucode_intel_ap(void);
int save_microcode_in_initrd_intel(void);
void reload_ucode_intel(void);
struct microcode_ops *init_intel_microcode(void);
#else /* CONFIG_CPU_SUP_INTEL */
static inline void load_ucode_intel_bsp(void) { }
static inline void load_ucode_intel_ap(void) { }
static inline int save_microcode_in_initrd_intel(void) { return -EINVAL; }
static inline void reload_ucode_intel(void) { }
static inline struct microcode_ops *init_intel_microcode(void) { return NULL; }
#endif  /* !CONFIG_CPU_SUP_INTEL */

#endif /* _X86_MICROCODE_INTERNAL_H */
