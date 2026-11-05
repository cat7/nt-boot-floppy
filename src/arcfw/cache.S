/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * cache.S -- make loaded code visible to the instruction fetch, and enter a program through
 * its {entry, toc} descriptor.  The 604's caches are coherent with the MMU on, so the sweep is
 * dcbf + icbi over the range by effective address (MPC604 User's Manual, cache management). */
	.text
	.p2align 2

/* VOID cache_sweep(ULONG start, ULONG len) */
	.globl cache_sweep
cache_sweep:
	addi	4, 4, 31
	srwi	4, 4, 5
	rlwinm	3, 3, 0, 0, 26
	mtctr	4
1:	dcbf	0, 3
	icbi	0, 3
	addi	3, 3, 32
	bdnz	1b
	sync
	isync
	blr

/* VOID cache_sweep_all(VOID): the loader asked for FlushAllCaches; the images it loaded are in
 * KSEG0's 8 MB, so sweep that */
	.globl cache_sweep_all
cache_sweep_all:
	lis	3, 0x8000
	lis	4, 0x0080
	b	cache_sweep

/* VOID arcfw_call_desc(PVOID desc, ULONG argc, PCHAR *argv, PCHAR *envp): the veneer's main, at
 * its end -- lwz r0,0(desc); mtlr; lwz r2,4(desc); blrl -- with the arguments moved down. */
	.globl arcfw_call_desc
arcfw_call_desc:
	lwz	0, 0(3)
	lwz	2, 4(3)
	mtctr	0
	mr	3, 4
	mr	4, 5
	mr	5, 6
	bctrl
	b	of_exit
