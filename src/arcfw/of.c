/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * of.c -- the Open Firmware client interface.  Every call is an array of cells: the service
 * name, the argument count, the return count, the arguments, then room for the returns; the
 * handler Open Firmware gave us in r5 takes its address and returns 0 if the service ran.
 * Original work, from IEEE 1275-1994 section 6.3 and the PowerPC binding. */
#include "arcfw.h"
#include "of.h"

OF_PHANDLE of_chosen;
OF_IHANDLE of_stdout, of_stdin, of_memory, of_mmu;

typedef struct { const CHAR *service; ULONG nargs, nrets; ULONG cell[10]; } OF_ARGS;

static LONG call(OF_ARGS *a) { return (LONG)of_call(a); }

OF_PHANDLE of_finddevice(const CHAR *path)
{
    OF_ARGS a = { "finddevice", 1, 1, { (ULONG)path, 0 } };
    return call(&a) ? OF_NONE : a.cell[1];
}
static OF_PHANDLE walk(const CHAR *svc, OF_PHANDLE node)
{
    OF_ARGS a = { svc, 1, 1, { node, 0 } };
    return call(&a) ? 0 : a.cell[1];
}
OF_PHANDLE of_peer(OF_PHANDLE n) { return walk("peer", n); }
OF_PHANDLE of_child(OF_PHANDLE n) { return walk("child", n); }
OF_PHANDLE of_parent(OF_PHANDLE n) { return walk("parent", n); }

LONG of_getproplen(OF_PHANDLE node, const CHAR *name)
{
    OF_ARGS a = { "getproplen", 2, 1, { node, (ULONG)name, 0 } };
    return call(&a) ? -1 : (LONG)a.cell[2];
}
LONG of_getprop(OF_PHANDLE node, const CHAR *name, PVOID buf, ULONG len)
{
    OF_ARGS a = { "getprop", 4, 1, { node, (ULONG)name, (ULONG)buf, len, 0 } };
    return call(&a) ? -1 : (LONG)a.cell[4];
}
LONG of_nextprop(OF_PHANDLE node, const CHAR *prev, CHAR *name32)
{
    OF_ARGS a = { "nextprop", 3, 1, { node, (ULONG)prev, (ULONG)name32, 0 } };
    return call(&a) ? -1 : (LONG)a.cell[3];
}
LONG of_setprop(OF_PHANDLE node, const CHAR *name, const VOID *buf, ULONG len)
{
    OF_ARGS a = { "setprop", 4, 1, { node, (ULONG)name, (ULONG)buf, len, 0 } };
    return call(&a) ? -1 : (LONG)a.cell[4];
}
LONG of_package_to_path(OF_PHANDLE node, CHAR *buf, ULONG len)
{
    OF_ARGS a = { "package-to-path", 3, 1, { node, (ULONG)buf, len, 0 } };
    LONG n = call(&a) ? -1 : (LONG)a.cell[3];
    if (n >= 0 && (ULONG)n < len) buf[n] = 0;
    return n;
}
OF_PHANDLE of_instance_to_package(OF_IHANDLE ih)
{
    OF_ARGS a = { "instance-to-package", 1, 1, { ih, 0 } };
    return call(&a) ? OF_NONE : a.cell[1];
}
LONG of_instance_to_path(OF_IHANDLE ih, CHAR *buf, ULONG len)
{
    OF_ARGS a = { "instance-to-path", 3, 1, { ih, (ULONG)buf, len, 0 } };
    LONG n = call(&a) ? -1 : (LONG)a.cell[3];
    if (n >= 0 && (ULONG)n < len) buf[n] = 0;
    return n;
}
OF_IHANDLE of_open(const CHAR *path)
{
    OF_ARGS a = { "open", 1, 1, { (ULONG)path, 0 } };
    return call(&a) ? 0 : a.cell[1];
}
VOID of_close(OF_IHANDLE ih)
{
    OF_ARGS a = { "close", 1, 0, { ih } };
    call(&a);
}
LONG of_read(OF_IHANDLE ih, PVOID buf, ULONG len)
{
    OF_ARGS a = { "read", 3, 1, { ih, (ULONG)buf, len, 0 } };
    return call(&a) ? -1 : (LONG)a.cell[3];
}
LONG of_write(OF_IHANDLE ih, const VOID *buf, ULONG len)
{
    OF_ARGS a = { "write", 3, 1, { ih, (ULONG)buf, len, 0 } };
    return call(&a) ? -1 : (LONG)a.cell[3];
}
LONG of_seek(OF_IHANDLE ih, ULONG pos_hi, ULONG pos_lo)
{
    OF_ARGS a = { "seek", 3, 1, { ih, pos_hi, pos_lo, 0 } };
    return call(&a) ? -1 : (LONG)a.cell[3];
}
PVOID of_claim(PVOID virt, ULONG size, ULONG align)
{
    OF_ARGS a = { "claim", 3, 1, { (ULONG)virt, size, align, 0 } };
    return call(&a) ? (PVOID)OF_NONE : (PVOID)a.cell[3];
}
VOID of_release(PVOID virt, ULONG size)
{
    OF_ARGS a = { "release", 2, 0, { (ULONG)virt, size } };
    call(&a);
}
ULONG of_milliseconds(VOID)
{
    OF_ARGS a = { "milliseconds", 0, 1, { 0 } };
    return call(&a) ? 0 : a.cell[0];
}
LONG of_interpret(const CHAR *forth)
{
    OF_ARGS a = { "interpret", 1, 1, { (ULONG)forth, 0 } };
    return call(&a) ? -1 : (LONG)a.cell[1];
}
LONG of_call_method(const CHAR *method, OF_IHANDLE ih, ULONG nin, ULONG nout, ULONG *in, ULONG *out)
{
    OF_ARGS a = { "call-method", nin + 2, nout + 1, { (ULONG)method, ih } };
    /* arguments are pushed in reverse: the last one first in the array? No -- IEEE 1275's
     * call-method takes them in order after the ihandle, the way the Forth stack picture reads
     * right to left; the veneer passes them (mode, size, virt, phys) for map. */
    for (ULONG i = 0; i < nin; i++) a.cell[2 + i] = in[i];
    if (call(&a)) return -1;
    if (a.cell[2 + nin]) return (LONG)a.cell[2 + nin];           /* catch result */
    for (ULONG i = 0; i < nout; i++) out[i] = a.cell[3 + nin + i];
    return 0;
}
VOID of_exit(VOID)
{
    OF_ARGS a = { "exit", 0, 0, { 0 } };
    call(&a);
    for (;;) { }
}
VOID of_enter(VOID)
{
    OF_ARGS a = { "enter", 0, 0, { 0 } };
    call(&a);
}

/* memory and mmu: the veneer's claimphys / map, measured in docs/2026-11-05-retiring-the-veneer.md.
 * claim on the memory node: ( [phys] size align -- base ); map on the mmu node:
 * ( phys virt size mode -- ).  As cells after the ihandle they go in the order the Forth stack
 * picture lists them, last on top, which is the reverse of this array; the veneer's register
 * assignments (r8 = mode, r9 = size, r10 = virt, stack = phys for map) are the pushes in that
 * order, i.e. the array holds them top-of-stack first. */
PVOID of_claim_phys(ULONG phys, ULONG size, ULONG align)
{
    ULONG in[3] = { align, size, phys }, out[1];
    if (of_call_method("claim", of_memory, 3, 1, in, out)) return (PVOID)OF_NONE;
    return (PVOID)out[0];
}
LONG of_map(ULONG phys, ULONG virt, ULONG size, LONG mode)
{
    ULONG in[4] = { (ULONG)mode, size, virt, phys };
    return of_call_method("map", of_mmu, 4, 0, in, NULL);
}
LONG of_claim_real(ULONG addr, ULONG size)
{
    if (of_claim_phys(addr & 0x7FFFFFFFu, size, 0) == (PVOID)OF_NONE) return -1;
    return of_map(addr & 0x7FFFFFFFu, addr, size, -1);
}

/* Integer properties are stored big-endian in the device tree, whatever mode the processor
 * runs in; this firmware runs little-endian (the N2 reboot), so every cell is swapped.
 * Measured 5 November: /chosen's stdout read as 0x00658dff, which is 0xff8d6500 -- an address
 * inside Open Firmware -- byte-reversed, and `write` on it answered -1. */
ULONG of_bswap(ULONG v) { return (v << 24) | ((v & 0xFF00u) << 8) | ((v >> 8) & 0xFF00u) | (v >> 24); }
LONG of_get_ints(OF_PHANDLE node, const CHAR *name, PULONG buf, ULONG len)
{
    LONG n = of_getprop(node, name, buf, len);
    for (LONG i = 0; i + 4 <= n; i += 4) buf[i / 4] = of_bswap(buf[i / 4]);
    return n;
}
BOOLEAN of_get_int(OF_PHANDLE node, const CHAR *name, PULONG out)
{
    ULONG v;
    if (of_get_ints(node, name, &v, 4) != 4) return FALSE;
    *out = v;
    return TRUE;
}
LONG of_get_str(OF_PHANDLE node, const CHAR *name, CHAR *buf, ULONG len)
{
    LONG n = of_getprop(node, name, buf, len - 1);
    if (n < 0) { buf[0] = 0; return -1; }
    buf[n] = 0;
    return n;
}
BOOLEAN of_has_prop(OF_PHANDLE node, const CHAR *name) { return of_getproplen(node, name) >= 0; }

VOID of_init(VOID)
{
    of_chosen = of_finddevice("/chosen");
    if (of_chosen == OF_NONE) of_exit();
    of_get_int(of_chosen, "stdout", &of_stdout);
    of_get_int(of_chosen, "stdin", &of_stdin);
    of_get_int(of_chosen, "memory", &of_memory);
    of_get_int(of_chosen, "mmu", &of_mmu);
}
