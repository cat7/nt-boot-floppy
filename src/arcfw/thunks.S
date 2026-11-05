/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * thunks.S -- the ARC firmware vector's entry points, as NT's loaders call them.
 *
 * A vector slot holds the address of an {entry, toc} descriptor; the caller loads the entry
 * into CTR, the toc into r2, and calls.  Our C functions want neither a TOC nor NT's frame:
 * NT keeps the caller's TOC at 4(r1) where SVR4 code saves LR, so a C function entered directly
 * would overwrite it (the HAL's wall 49).  Each slot therefore names a thunk that opens a
 * 64-byte frame of its own, calls the C function, and returns through it -- mkstubs.py's
 * xthunk shape, which the HAL's exports have used since September. */
	.macro ARC name
	.data
	.p2align 3
	.globl desc_\name
desc_\name:	.long thunk_\name
	.long 0
	.text
	.p2align 2
	.type thunk_\name,@function
thunk_\name:
	mflr	0
	stwu	1, -64(1)
	stw	0, 60(1)
	bl	arc_\name
	lwz	0, 60(1)
	addi	1, 1, 64
	mtlr	0
	blr
	.endm

	ARC Load
	ARC Invoke
	ARC Execute
	ARC Halt
	ARC PowerDown
	ARC Restart
	ARC Reboot
	ARC EnterInteractiveMode
	ARC GetPeer
	ARC GetChild
	ARC GetParent
	ARC GetConfigurationData
	ARC AddChild
	ARC DeleteComponent
	ARC GetComponent
	ARC SaveConfiguration
	ARC GetSystemId
	ARC GetMemoryDescriptor
	ARC GetTime
	ARC GetRelativeTime
	ARC GetDirectoryEntry
	ARC Open
	ARC Close
	ARC Read
	ARC GetReadStatus
	ARC Write
	ARC Seek
	ARC Mount
	ARC GetEnvironmentVariable
	ARC SetEnvironmentVariable
	ARC GetFileInformation
	ARC SetFileInformation
	ARC FlushAllCaches
	ARC TestUnicodeCharacter
	ARC GetDisplayStatus
