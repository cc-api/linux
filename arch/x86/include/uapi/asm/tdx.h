/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_X86_TDX_H
#define _UAPI_ASM_X86_TDX_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* Length of the REPORTDATA used in TDG.MR.REPORT TDCALL */
#define TDX_REPORTDATA_LEN		64

/* Length of TDREPORT used in TDG.MR.REPORT TDCALL */
#define TDX_REPORT_LEN			1024

/**
 * struct tdx_report_req: Get TDREPORT using REPORTDATA as input.
 *
 * @reportdata : User-defined 64-Byte REPORTDATA to be included into
 *		 TDREPORT. Typically it can be some nonce provided by
 *		 attestation service, so the generated TDREPORT can be
 *		 uniquely verified.
 * @tdreport   : TDREPORT output from TDCALL[TDG.MR.REPORT] of size
 *		 TDX_REPORT_LEN.
 *
 * Used in TDX_CMD_GET_REPORT IOCTL request.
 */
struct tdx_report_req {
	union {
		__u8 reportdata[TDX_REPORTDATA_LEN];
		__u8 tdreport[TDX_REPORT_LEN];
	};
};

/*
 * TDX_CMD_GET_REPORT - Get TDREPORT using TDCALL[TDG.MR.REPORT]
 *
 * Return 0 on success, -EIO on TDCALL execution failure, and
 * standard errno on other general error cases.
 *
 */
#define TDX_CMD_GET_REPORT		_IOWR('T', 0x01, struct tdx_report_req)

#endif /* _UAPI_ASM_X86_TDX_H */
