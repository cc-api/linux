// SPDX-License-Identifier: GPL-2.0
/*
 * attest.c - TDX guest attestation interface driver.
 *
 * Implements user interface to trigger attestation process.
 *
 * Copyright (C) 2022 Intel Corporation
 *
 */

#define pr_fmt(fmt) "x86/tdx: attest: " fmt

#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <asm/tdx.h>
#include <uapi/asm/tdx.h>

#define DRIVER_NAME "tdx-attest"

/* TDREPORT module call leaf ID */
#define TDX_GET_REPORT			4

static struct miscdevice miscdev;

static long tdx_get_report(void __user *argp)
{
	void *reportdata = NULL, *tdreport = NULL;
	long ret;

	/* Allocate buffer space for REPORTDATA */
	reportdata = kmalloc(TDX_REPORTDATA_LEN, GFP_KERNEL);
	if (!reportdata)
		return -ENOMEM;

	/* Allocate buffer space for TDREPORT */
	tdreport = kmalloc(TDX_REPORT_LEN, GFP_KERNEL);
	if (!tdreport) {
		ret = -ENOMEM;
		goto out;
	}

	/* Copy REPORTDATA from the user buffer */
	if (copy_from_user(reportdata, argp, TDX_REPORTDATA_LEN)) {
		ret = -EFAULT;
		goto out;
	}

	/*
	 * Generate TDREPORT using "TDG.MR.REPORT" TDCALL.
	 *
	 * Get the TDREPORT using REPORTDATA as input. Refer to
	 * section 22.3.3 TDG.MR.REPORT leaf in the TDX Module 1.0
	 * Specification for detailed information.
	 */
	ret = __tdx_module_call(TDX_GET_REPORT, virt_to_phys(tdreport),
				virt_to_phys(reportdata), 0, 0, NULL);
	if (ret) {
		pr_debug("TDREPORT TDCALL failed, status:%lx\n", ret);
		ret = -EIO;
		goto out;
	}

	/* Copy TDREPORT back to the user buffer */
	if (copy_to_user(argp, tdreport, TDX_REPORT_LEN))
		ret = -EFAULT;

out:
	kfree(reportdata);
	kfree(tdreport);
	return ret;
}

static long tdx_attest_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	long ret = -EINVAL;

	switch (cmd) {
	case TDX_CMD_GET_REPORT:
		ret = tdx_get_report(argp);
		break;
	default:
		pr_debug("cmd %d not supported\n", cmd);
		break;
	}

	return ret;
}

static const struct file_operations tdx_attest_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= tdx_attest_ioctl,
	.llseek		= no_llseek,
};

static int __init tdx_attestation_init(void)
{
	int ret;

	/* Make sure we are in a valid TDX platform */
	if (!cpu_feature_enabled(X86_FEATURE_TDX_GUEST))
		return -EIO;

	miscdev.name = DRIVER_NAME;
	miscdev.minor = MISC_DYNAMIC_MINOR;
	miscdev.fops = &tdx_attest_fops;

	ret = misc_register(&miscdev);
	if (ret) {
		pr_err("misc device registration failed\n");
		return ret;
	}

	return 0;
}
device_initcall(tdx_attestation_init)
