/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * of.h -- the Open Firmware client interface (IEEE 1275-1994, the PowerPC binding), as this
 * shim uses it.  Original work, written from the standard.  A phandle names a device-tree node,
 * an ihandle an opened instance; both are 32-bit cells here. */
#pragma once
#include "arcfw.h"

typedef ULONG OF_PHANDLE;
typedef ULONG OF_IHANDLE;
#define OF_NONE ((ULONG)-1)

extern ULONG of_cif, of_entry_r1;
ULONG of_call(PVOID args);

OF_PHANDLE of_finddevice(const CHAR *path);
OF_PHANDLE of_peer(OF_PHANDLE node);
OF_PHANDLE of_child(OF_PHANDLE node);
OF_PHANDLE of_parent(OF_PHANDLE node);
LONG of_getproplen(OF_PHANDLE node, const CHAR *name);
LONG of_getprop(OF_PHANDLE node, const CHAR *name, PVOID buf, ULONG len);
LONG of_nextprop(OF_PHANDLE node, const CHAR *prev, CHAR *name32);
LONG of_setprop(OF_PHANDLE node, const CHAR *name, const VOID *buf, ULONG len);
LONG of_package_to_path(OF_PHANDLE node, CHAR *buf, ULONG len);
OF_PHANDLE of_instance_to_package(OF_IHANDLE ih);
LONG of_instance_to_path(OF_IHANDLE ih, CHAR *buf, ULONG len);
OF_IHANDLE of_open(const CHAR *path);
VOID of_close(OF_IHANDLE ih);
LONG of_read(OF_IHANDLE ih, PVOID buf, ULONG len);
LONG of_write(OF_IHANDLE ih, const VOID *buf, ULONG len);
LONG of_seek(OF_IHANDLE ih, ULONG pos_hi, ULONG pos_lo);
PVOID of_claim(PVOID virt, ULONG size, ULONG align);
VOID of_release(PVOID virt, ULONG size);
ULONG of_milliseconds(VOID);
LONG of_interpret(const CHAR *forth);
/* call-method with up to 6 cells in and 3 out; returns the catch result (0 = ran) */
LONG of_call_method(const CHAR *method, OF_IHANDLE ih, ULONG nin, ULONG nout, ULONG *in, ULONG *out);
VOID of_exit(VOID) __attribute__((noreturn));
VOID of_enter(VOID);

/* the memory and mmu packages /chosen names: physical claims and virtual mappings */
PVOID of_claim_phys(ULONG phys, ULONG size, ULONG align);   /* OF_NONE on failure */
LONG of_map(ULONG phys, ULONG virt, ULONG size, LONG mode);
LONG of_claim_real(ULONG addr, ULONG size);                 /* claim phys = virt, then map */

/* property helpers */
BOOLEAN of_get_int(OF_PHANDLE node, const CHAR *name, PULONG out);
LONG of_get_str(OF_PHANDLE node, const CHAR *name, CHAR *buf, ULONG len);   /* -1 if absent */
BOOLEAN of_has_prop(OF_PHANDLE node, const CHAR *name);

extern OF_PHANDLE of_chosen;
extern OF_IHANDLE of_stdout, of_stdin, of_memory, of_mmu;
VOID of_init(VOID);
