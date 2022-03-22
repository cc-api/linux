// SPDX-License-Identifier: GPL-2.0
/* Load and initialize TDX-module. */

#define pr_fmt(fmt) "tdx: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/cpu.h>

#include <asm/irq_vectors.h>
#include <asm/tdx_host.h>
#include <asm/cmdline.h>
#include <asm/virtext.h>

#include "p-seamldr-old.h"
#include "seam-old.h"

enum TDX_HOST_OPTION {
	TDX_HOST_OFF,
	TDX_HOST_INITRD,
};

static enum TDX_HOST_OPTION tdx_host __initdata;

/* Later tdx_host will be overwritten by tdx_host_setup(). */
static void __init tdx_host_param(void)
{
	char arg[7];
	int ret;

	ret = cmdline_find_option(boot_command_line, "tdx_host",
				arg, sizeof(arg));
	if (ret == 6 && !strncmp(arg, "initrd", 6))
		tdx_host = TDX_HOST_INITRD;
	if (ret == 2 && !strncmp(arg, "on", 2))
		tdx_host = TDX_HOST_INITRD;
}

void __init tdx_early_init(void)
{
	/*
	 * It's early boot phase before kernel param() and __setup() are usable.
	 */
	tdx_host_param();

	/* Try to load P-SEAMLDR from initrd. */
	if (tdx_host != TDX_HOST_INITRD)
		return;

	/* TDX requires SEAM mode. */
	if (!is_seamrr_enabled())
		return;

	/* TDX(SEAMCALL) requires VMX. */
	if (__seam_init_vmx_early())
		return;

	/* Try to load P-SEAMLDR from initrd. */
	load_p_seamldr();
}

/*
 * free_seamldr_params - free allocated for seamldr_params including referenced
 *			 pages by params.
 * @params: virtual address of struct seamldr_params to free
 */
static void __init free_seamldr_params(struct seamldr_params *params)
{
	int i;

	if (!params)
		return;

	for (i = 0; i < params->num_module_pages; i++)
		if (params->mod_pages_pa_list[i])
			free_page((unsigned long)__va(params->mod_pages_pa_list[i]));
	if (params->sigstruct_pa)
		free_page((unsigned long)__va(params->sigstruct_pa));
	free_page((unsigned long)params);
}

/*
 * alloc_seamldr_params - initialize parameters for P-SEAMLDR to load TDX module.
 * @module: virtual address of TDX module.
 * @module_size: size of module.
 * @sigstruct: virtual address of sigstruct of TDX module.
 * @sigstruct_size: size of sigstruct of TDX module.
 * @scenario: SEAMLDR_SCENARIO_LOAD or SEAMLDR_SCENARIO_UPDATE.
 * @return: pointer to struct seamldr_params on success, error code on failure.
 *
 * Allocate and initialize struct seamldr_params for P-SEAMLDR to load TDX
 * module.  Memory for seamldr_params and members is required to be 4K
 * page-aligned.  Use free_seamldr_params() to free allocated pages including
 * referenced by params.
 *
 * KASAN thinks memcpy from initrd image via cpio image invalid access.
 * Here module and sigstruct come from initrd image, not from memory allocator.
 * Annotate it with __no_sanitize_address to apiece KASAN.
 */
static struct seamldr_params * __init __no_sanitize_address alloc_seamldr_params(
	const void *module, unsigned long module_size, const void *sigstruct,
	unsigned long sigstruct_size, u64 scenario)
{
	struct seamldr_params *params = NULL;
	void *sigstruct_page = NULL;
	void *module_page = NULL;
	int i;

	BUILD_BUG_ON(SEAMLDR_SIGSTRUCT_SIZE > PAGE_SIZE);

	/*
	 * SEAM module must be equal or less than
	 * SEAMLDR_MAX_NR_MODULE_PAGES(496) pages.
	 */
	if (!module_size ||
	    module_size > SEAMLDR_MAX_NR_MODULE_PAGES * PAGE_SIZE) {
		pr_err("Invalid SEAM module size 0x%lx\n", module_size);
		return ERR_PTR(-EINVAL);
	}
	/*
	 * SEAM signature structure must be SEAMLDR_SIGSTRUCT_SIZE(2048) bytes.
	 */
	if (sigstruct_size != SEAMLDR_SIGSTRUCT_SIZE) {
		pr_err("Invalid SEAM signature structure size 0x%lx\n",
		       sigstruct_size);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * Allocate and initialize the SEAMLDR params.  Pages are passed in as
	 * a list of physical addresses.
	 *
	 * params must be 4K aligned.
	 */
	params = (struct seamldr_params *)get_zeroed_page(GFP_KERNEL);
	if (!params) {
		pr_err("Unable to allocate memory for SEAMLDR_PARAMS\n");
		goto out;
	}
	params->scenario = scenario;

	/* SEAMLDR requires the sigstruct to be 4K aligned. */
	sigstruct_page = (void *)__get_free_page(GFP_KERNEL);
	if (!sigstruct_page) {
		pr_err("Unable to allocate memory to copy sigstruct\n");
		goto out;
	}
	memcpy(sigstruct_page, sigstruct, sigstruct_size);
	params->sigstruct_pa = __pa(sigstruct_page);

	params->num_module_pages = PFN_UP(module_size);
	for (i = 0; i < params->num_module_pages; i++) {
		module_page = (void *)__get_free_page(GFP_KERNEL);
		if (!module_page) {
			pr_err("Unable to allocate memory to copy SEAM module\n");
			goto out;
		}
		params->mod_pages_pa_list[i] = __pa(module_page);
		memcpy(module_page, module + i * PAGE_SIZE,
		       min(module_size, PAGE_SIZE));
		if (module_size < PAGE_SIZE)
			memset(module_page + module_size, 0,
			       PAGE_SIZE - module_size);
		module_size -= PAGE_SIZE;
	}

	return params;

out:
	free_seamldr_params(params);
	return ERR_PTR(-ENOMEM);
}

struct tdx_install_module_data {
	struct seamldr_params *params;
	atomic_t error;
};

/* Load seam module on one CPU */
static void __init tdx_install_module_cpu(void *data)
{
	struct tdx_install_module_data *install_module = data;
	int ret = seamldr_install(__pa(install_module->params));

	if (ret)
		atomic_set(&install_module->error, ret);
}

#define TDX_MODULE_NAME		"kernel/x86/tdx/libtdx.bin"
#define TDX_SIGSTRUCT_NAME	"kernel/x86/tdx/libtdx.bin.sigstruct"

/* load TDX module on all CPUs through P-SEAMLDR */
static int __init tdx_load_module(void)
{
	struct tdx_install_module_data install_module;
	struct cpio_data module, sigstruct;
	struct seamldr_params *params;
	int ret = 0;
	int cpu;

	pr_info("Loading TDX module via P-SEAMLDR with %s and %s\n",
		TDX_MODULE_NAME, TDX_SIGSTRUCT_NAME);

	if (!seam_get_firmware(&module, TDX_MODULE_NAME) ||
		!seam_get_firmware(&sigstruct, TDX_SIGSTRUCT_NAME)) {
		return -ENOENT;
	}

	params = alloc_seamldr_params(module.data, module.size, sigstruct.data,
				sigstruct.size, SEAMLDR_SCENARIO_LOAD);
	if (IS_ERR(params))
		return -ENOMEM;

	install_module.params = params;
	atomic_set(&install_module.error, 0);
	/*
	 * SEAMLDR.INSTALL requires serialization.  Call the function on each
	 * CPUs one by one to avoid NMI watchdog instead of contending for
	 * a spinlock.  If there are many CPUs (hundreds of CPUs is enough),
	 * tdx_install_module_cpu() may contend for long time to trigger NMI
	 * watchdog.
	 */
	for_each_online_cpu(cpu) {
		smp_call_function_single(cpu, tdx_install_module_cpu,
					&install_module, 1);
		ret = atomic_read(&install_module.error);
		if (ret)
			goto out;
	}

out:
	free_seamldr_params(params);
	return ret;
}

/*
 * Early system wide initialization of the TDX module. Check if the TDX firmware
 * loader and the TDX firmware module are available and log their version.
 */
static int __init tdx_arch_init(void)
{
	int vmxoff_err;
	int ret = 0;

	/* Avoid TDX overhead when opt-in is not present. */
	if (tdx_host == TDX_HOST_OFF)
		return 0;

	/* TDX requires SEAM mode. */
	if (!is_seamrr_enabled())
		return -EOPNOTSUPP;

	/* TDX requires VMX. */
	ret = seam_init_vmx_early();
	if (ret)
		return ret;

	/*
	 * Check if P-SEAMLDR is available and log its version information for
	 * the administrator of the machine.  Although the kernel don't use
	 * P-SEAMLDR at the moment, it's a part of TCB.  It's worthwhile to
	 * tell it to the administrator of the machine.
	 */
	ret = p_seamldr_get_info();
	if (ret) {
		pr_info("No P-SEAMLDR is available.\n");
		return ret;
	}
	setup_force_cpu_cap(X86_FEATURE_SEAM);

	/*
	 * Prevent potential concurrent CPU online/offline because smp is
	 * enabled.
	 * - Make seam_vmx{on, off}_on_each_cpu() work.  Otherwise concurrently
	 *   onlined CPU has VMX disabled and the SEAM operation on that CPU
	 *   fails.
	 * - Ensure all present CPUs are online during this initialization after
	 *   the check.
	 */
	cpus_read_lock();

	/*
	 * Initialization of TDX module needs to involve all CPUs.  Ensure all
	 * CPUs are online.  All CPUs are required to be initialized by
	 * TDH.SYS.LP.INIT otherwise TDH.SYS.CONFIG fails.
	 */
	if (!cpumask_equal(cpu_present_mask, cpu_online_mask)) {
		ret = -EINVAL;
		goto out_err;
	}

	/* SEAMCALL requires to enable VMX on CPUs. */
	ret = seam_alloc_init_vmcs_tmp_set();
	if (ret)
		goto out_err;
	ret = seam_vmxon_on_each_cpu();
	if (ret)
		goto out;

	ret = tdx_load_module();
	if (ret) {
		pr_info("Failed to load TDX module.\n");
		goto out;
	}
	pr_info("Loaded TDX module via P-SEAMLDR.\n");

out:
	/*
	 * Other codes (especially kvm_intel) expect that they're the first to
	 * use VMX.  That is, VMX is off on their initialization as a reset
	 * state.  Maintain the assumption to keep them working.
	 */
	vmxoff_err = seam_vmxoff_on_each_cpu();
	if (vmxoff_err) {
		pr_info("Failed to VMXOFF.\n");
		if (!ret)
			ret = vmxoff_err;
	}
	seam_free_vmcs_tmp_set();

out_err:
	cpus_read_unlock();

	if (ret)
		pr_err("Failed to find the TDX module. %d\n", ret);

	return ret;
}

/*
 * arch_initcall() is chosen to satisfy the following conditions.
 * - After SMP initialization.
 */
arch_initcall(tdx_arch_init);
