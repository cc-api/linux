/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/export.h>
#include <asm/frame.h>

#include "tdxcall.S"

/*
 * __seamcall() - Host-side interface functions to SEAM software module
 *		  (the P-SEAMLDR or the TDX module).
 *
 * Transform function call register arguments into the SEAMCALL register
 * ABI.  Return TDX_SEAMCALL_VMFAILINVALID if the SEAMCALL itself fails,
 * or the completion status of the SEAMCALL leaf function.  Additional
 * output operands are saved in @out (if it is provided by the caller).
 *
 *-------------------------------------------------------------------------
 * SEAMCALL ABI:
 *-------------------------------------------------------------------------
 * Input Registers:
 *
 * RAX                 - SEAMCALL Leaf number.
 * RCX,RDX,R8-R13      - SEAMCALL Leaf specific input registers.
 *
 * Output Registers:
 *
 * RAX                 - SEAMCALL completion status code.
 * RCX,RDX,R8-R13      - SEAMCALL Leaf specific output registers.
 *
 *-------------------------------------------------------------------------
 *
 * __seamcall() function ABI:
 *
 * @fn  (RDI)          - SEAMCALL Leaf number, moved to RAX
 * @rcx (RSI)          - Input parameter 1, moved to RCX
 * @rdx (RDX)          - Input parameter 2, moved to RDX
 * @r8  (RCX)          - Input parameter 3, moved to R8
 * @r9  (R8)           - Input parameter 4, moved to R9
 * @r10 (R9)           - Input parameter 5, moved to R10
 * @r11 (stack param 1)- Input parameter 6, moved to R11
 * @r12 (stack param 2)- Input parameter 7, moved to R12
 * @r13 (stack param 3)- Input parameter 8, moved to R13
 *
 * @out (stack param 4)- struct tdx_module_output pointer
 *                       stored temporarily in R15 (not
 *                       used by the P-SEAMLDR or the TDX
 *                       module). It can be NULL.
 *
 * Return (via RAX) the completion status of the SEAMCALL, or
 * TDX_SEAMCALL_VMFAILINVALID.
 */
SYM_FUNC_START(__seamcall)
	FRAME_BEGIN
	TDX_MODULE_CALL host=1
	FRAME_END
	RET
SYM_FUNC_END(__seamcall)
EXPORT_SYMBOL_GPL(__seamcall)
