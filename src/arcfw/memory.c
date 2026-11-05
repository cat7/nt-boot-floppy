/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * memory.c -- the ARC memory descriptors, from Open Firmware's /memory node.
 *
 * `reg` is the installed memory, `available` what Open Firmware has not claimed.  The list the
 * loader walks (GetMemoryDescriptor) covers all of `reg`: available ranges are MemoryFree, the
 * loaded program's pages MemoryLoadedProgram, the first ten pages (exception vectors, the
 * system parameter block at 0x4000) MemoryFirmwarePermanent, and everything else -- this
 * image, Open Firmware's own memory at real-base, and all memory above the 8 MB Open Firmware
 * manages -- MemoryFirmwareTemporary, which the kernel reclaims once it has taken over.  That is
 * the veneer's policy exactly, measured in the trace of 5 November (docs/2026-11-05-the-arc-
 * firmware.md): descriptors 0-0xA permanent, 0x40-0x78 temporary (the veneer), 0x600-0x64B the
 * loader, 0x800-0x3D00 temporary, the Open Firmware pieces at 0x3D00.. temporary.  The HAL and
 * the kernel cope with that layout, so it is kept.  Original. */
#include "arcfw.h"
#include "of.h"

#define MAX_DESC 64
static MEMORY_DESCRIPTOR descs[MAX_DESC];
static ULONG ndesc;
static ULONG loaded_base, loaded_pages;      /* the program load_image placed, in pages */

static VOID add(MEMORY_TYPE type, ULONG base, ULONG count)
{
    if (!count) return;
    if (ndesc && descs[ndesc - 1].MemoryType == (ULONG)type && descs[ndesc - 1].BasePage + descs[ndesc - 1].PageCount == base) {
        descs[ndesc - 1].PageCount += count;        /* merge with the previous one */
        return;
    }
    if (ndesc == MAX_DESC) fatal("too many memory descriptors");
    descs[ndesc].MemoryType = type; descs[ndesc].BasePage = base; descs[ndesc].PageCount = count;
    ndesc++;
}

/* type a page range by what it is, splitting on the special ranges */
static VOID type_range(ULONG base, ULONG count, MEMORY_TYPE deflt)
{
    while (count) {
        ULONG end = base + count, cut = end;
        MEMORY_TYPE t = deflt;
        if (base < 0xA) { t = MemoryFirmwarePermanent; cut = 0xA < end ? 0xA : end; }
        else if (base >= loaded_base && base < loaded_base + loaded_pages) {
            t = MemoryLoadedProgram; cut = loaded_base + loaded_pages < end ? loaded_base + loaded_pages : end;
        } else {
            if (loaded_base > base && loaded_base < cut) cut = loaded_base;
        }
        add(t, base, cut - base);
        count -= cut - base; base = cut;
    }
}

VOID memory_note_claim(ULONG phys, ULONG size, MEMORY_TYPE type)
{
    if (type == MemoryLoadedProgram) { loaded_base = phys >> 12; loaded_pages = (size + 0xFFF) >> 12; }
}

VOID memory_init(VOID)
{
    OF_PHANDLE mem = of_finddevice("/memory");
    if (mem == OF_NONE) fatal("no /memory node");
    ULONG reg[32], avail[64];
    LONG nreg = of_getprop(mem, "reg", reg, sizeof reg);
    LONG navail = of_getprop(mem, "available", avail, sizeof avail);
    if (nreg < 8) fatal("/memory has no reg property");
    if (navail < 0) navail = 0;
    ndesc = 0;
    DBG(DBG_MEM, "arcfw: memory: %d reg cells, %d available cells\n", nreg / 4, navail / 4);
    /* walk installed memory in page units; inside it, the available ranges are free */
    for (LONG i = 0; i + 1 < nreg / 4; i += 2) {
        ULONG rbase = reg[i] >> 12, rend = (reg[i] + reg[i + 1]) >> 12;
        ULONG at = rbase;
        /* available ranges, assumed sorted ascending as Open Firmware keeps them */
        for (LONG j = 0; j + 1 < navail / 4; j += 2) {
            ULONG abase = avail[j] >> 12, aend = (avail[j] + avail[j + 1] + 0xFFF) >> 12;
            if (aend <= at || abase >= rend) continue;
            if (abase < at) abase = at;
            if (aend > rend) aend = rend;
            if (abase > at) type_range(at, abase - at, MemoryFirmwareTemporary);
            type_range(abase, aend - abase, MemoryFree);
            at = aend;
        }
        if (at < rend) type_range(at, rend - at, MemoryFirmwareTemporary);
    }
    for (ULONG k = 0; k < ndesc; k++)
        DBG(DBG_MEM, "arcfw:   descriptor %u: type %u pages %05x..%05x\n", k, descs[k].MemoryType, descs[k].BasePage, descs[k].BasePage + descs[k].PageCount);
}

PMEMORY_DESCRIPTOR memory_next(PMEMORY_DESCRIPTOR prev)
{
    if (!ndesc) return NULL;
    if (prev == NULL) return &descs[0];
    ULONG i = (ULONG)(prev - descs);
    if (i + 1 >= ndesc) return NULL;
    return &descs[i + 1];
}
