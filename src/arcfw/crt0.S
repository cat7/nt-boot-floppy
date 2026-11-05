/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * crt0.S -- how Open Firmware enters a client program, and how this one leaves.
 *
 * `go` after pe-loader's init-program jumps to ImageBase + AddressOfEntryPoint with the IEEE
 * 1275 PowerPC client-interface binding in force: 5 is the client-interface handler, 1 a
 * stack Open Firmware lent us, and the MMU on with Open Firmware's translations (real-mode?
 * false; load-base and real-base are what setup.of put in NVRAM).  Microsoft's veneer saves 5
 * and calls its main; so does this.  The stack is ours (bss), so nothing of Open Firmware's is
 * written below its own frames. */
	.section .text.start,"ax",@progbits
	.globl _start
_start:
	lis	11, of_cif@ha
	stw	5, of_cif@l(11)
	lis	11, of_entry_r1@ha
	stw	1, of_entry_r1@l(11)
	/* clear .bss */
	lis	11, __bss_start@ha
	addi	11, 11, __bss_start@l
	lis	12, __bss_end@ha
	addi	12, 12, __bss_end@l
	li	0, 0
1:	cmplw	11, 12
	bge	2f
	stw	0, 0(11)
	addi	11, 11, 4
	b	1b
2:	lis	1, arcfw_stack_top@ha
	addi	1, 1, arcfw_stack_top@l
	addi	1, 1, -64
	li	0, 0
	stw	0, 0(1)
	bl	arcfw_main
	/* main returned: back to Open Firmware */
	b	of_exit

/* ULONG of_call(PVOID args): the client interface call.  3 = the argument array; returns the
 * handler's 3 (0 = the service ran, -1 = it does not exist).  Open Firmware may use the
 * caller's stack, so a proper frame. */
	.text
	.globl of_call
of_call:
	mflr	0
	stwu	1, -32(1)
	stw	0, 36(1)
	stw	2, 28(1)
	lis	11, of_cif@ha
	lwz	11, of_cif@l(11)
	mtctr	11
	bctrl
	lwz	2, 28(1)
	lwz	0, 36(1)
	addi	1, 1, 32
	mtlr	0
	blr

/* VOID arcfw_enter(PVOID entry, ULONG argc, PCHAR *argv, PCHAR *envp): into the loaded program,
 * as the veneer's PxInvoke does -- a bare branch with the C arguments in 3..5.  NT's loaders
 * find their own TOC. */
	.globl arcfw_enter
arcfw_enter:
	mtctr	3
	mr	3, 4
	mr	4, 5
	mr	5, 6
	bctr

/* The two words saved at entry live in .data: _start stores them BEFORE it clears bss, and a
 * first build had them in bss -- the client-interface pointer was zeroed and the first call into
 * Open Firmware jumped to address 0 (program check at SRR0 0, 5 November). */
	.data
	.globl of_cif, of_entry_r1
	.balign 8
of_cif:		.long 0
of_entry_r1:	.long 0

	.section .bss
	.globl arcfw_stack_top
	.balign 16
arcfw_stack:	.space 65536
arcfw_stack_top:
