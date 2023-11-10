/***************************************************************************/
/**                   I N T E L  C O N F I D E N T I A L                  **/
/***************************************************************************/
/**                                                                       **/
/**  Copyright (c) 2023 Intel Corporation                                 **/
/**                                                                       **/
/**  This program contains proprietary and confidential information.      **/
/**  All rights reserved.                                                 **/
/**                                                                       **/
/***************************************************************************/
/**                   I N T E L  C O N F I D E N T I A L                  **/
/***************************************************************************/

#include "dis-asm.h"

typedef unsigned long kdb_machreg_t;

/*
 * kdba_id_parsemode
 *
 * 	Parse IDMODE environment variable string and
 *	set appropriate value into "disassemble_info" structure.
 *
 * Parameters:
 *	mode	Mode string
 *	dip	Disassemble_info structure pointer
 * Returns:
 * Locking:
 * Remarks:
 *	We handle the values 'x86' and '8086' to enable either
 *	32-bit instruction set or 16-bit legacy instruction set.
 */
int kdba_id_parsemode(const char *mode, disassemble_info *dip);

/*
 * kdba_check_pc
 *
 * 	Check that the pc is satisfactory.
 *
 * Parameters:
 *	pc	Program Counter Value.
 * Returns:
 *	None
 * Locking:
 *	None.
 * Remarks:
 *	Can change pc.
 */

void kdba_check_pc(kdb_machreg_t *pc);

/*
 * kdba_id_printinsn
 *
 * 	Format and print a single instruction at 'pc'. Return the
 *	length of the instruction.
 *
 * Parameters:
 *	pc	Program Counter Value.
 *	dip	Disassemble_info structure pointer
 * Returns:
 *	Length of instruction, -1 for error.
 * Locking:
 *	None.
 * Remarks:
 *	Depends on 'IDMODE' environment variable.
 */

int kdba_id_printinsn(kdb_machreg_t pc, disassemble_info *dip);

/*
 * kdba_id_init
 *
 * 	Initialize the architecture dependent elements of
 *	the disassembly information structure
 *	for the GNU disassembler.
 *
 * Parameters:
 *	None.
 * Outputs:
 *	None.
 * Returns:
 *	None.
 * Locking:
 *	None.
 * Remarks:
 */

void kdba_id_init(disassemble_info *dip);
