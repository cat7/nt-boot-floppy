/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * load.c -- loading NT's loader (OSLOADER.EXE, SETUPLDR) and starting it.
 *
 * Both are raw COFF images (no MZ stub) linked at an address in KSEG0 -- OSLOADER.EXE at
 * 0x80600000, 0x4A800 bytes -- and expect to run there, without relocation.  The image is read
 * through the ARC file it was opened as; each section lands at ImageBase + its RVA, in memory
 * claimed at the matching physical address and mapped at its virtual one (the veneer's
 * claimreal).  AddressOfEntryPoint names an {entry, toc} descriptor; the program is entered
 * with the toc in r2 and argc, argv, envp in r3..r5, as the veneer's main did it.  Original. */
#include "arcfw.h"
#include "of.h"

extern VOID cache_sweep(ULONG start, ULONG len);
extern VOID arcfw_call_desc(PVOID desc, ULONG argc, PCHAR *argv, PCHAR *envp) __attribute__((noreturn));

static UCHAR *whole; static ULONG whole_size;     /* the file in memory, when it came that way */

static ARC_STATUS read_at(ULONG fd, ULONG off, PVOID buf, ULONG n)
{
    if (whole) {
        if (off + n > whole_size || off + n < off) return EIO;
        memcpy(buf, whole + off, n);
        return ESUCCESS;
    }
    LARGE_INTEGER li; li.HighPart = 0; li.LowPart = off;
    ULONG got;
    ARC_STATUS s = io_seek(fd, &li, SeekAbsolute);
    if (s != ESUCCESS) return s;
    s = io_read(fd, buf, n, &got);
    if (s != ESUCCESS) return s;
    return got == n ? ESUCCESS : EIO;
}

ARC_STATUS load_image(const CHAR *arcpath, PVOID *entry_desc, PVOID *low, PVOID *high)
{
    ULONG fd;
    ARC_STATUS s = io_open(arcpath, ArcOpenReadOnly, &fd);
    if (s != ESUCCESS) { con_printf("arcfw: cannot open '%s' (%u)\n", arcpath, s); return s; }
    UCHAR hdr[0x100];
    ULONG base_off = 0;
    whole = NULL;
    if ((s = read_at(fd, 0, hdr, 0x40)) != ESUCCESS) {
        /* a file Open Firmware will not read piecemeal (its ISO 9660 package): whole, then */
        PVOID w; ULONG wn;
        if (io_load_whole(fd, &w, &wn) != ESUCCESS) goto out;
        whole = w; whole_size = wn;
        if ((s = read_at(fd, 0, hdr, 0x40)) != ESUCCESS) goto out;
    }
    if (hdr[0] == 'M' && hdr[1] == 'Z') {                  /* a PE: the COFF header after the stub */
        ULONG pe = *(ULONG *)&hdr[0x3C];
        if ((s = read_at(fd, pe, hdr, 4)) != ESUCCESS) goto out;
        if (memcmp(hdr, "PE\0\0", 4) != 0) { s = ENOEXEC; goto out; }
        base_off = pe + 4;
    }
    if ((s = read_at(fd, base_off, hdr, 20)) != ESUCCESS) goto out;
    USHORT machine = *(USHORT *)&hdr[0], nsec = *(USHORT *)&hdr[2], optsz = *(USHORT *)&hdr[16];
    if (machine != 0x1F0) { con_printf("arcfw: '%s' is not a PowerPC image (machine %x)\n", arcpath, machine); s = ENOEXEC; goto out; }
    if (optsz < 0x40 || optsz > sizeof hdr) { s = ENOEXEC; goto out; }
    if ((s = read_at(fd, base_off + 20, hdr, optsz)) != ESUCCESS) goto out;
    if (*(USHORT *)&hdr[0] != 0x10B) { s = ENOEXEC; goto out; }
    ULONG entry_rva = *(ULONG *)&hdr[16], image_base = *(ULONG *)&hdr[28], image_size = *(ULONG *)&hdr[56];
    DBG(DBG_LOAD, "arcfw: image base %08x size %x entry rva %x, %u sections\n", image_base, image_size, entry_rva, nsec);
    if (of_claim_real(image_base, (image_size + 0xFFF) & ~0xFFFu) < 0) {
        con_printf("arcfw: cannot claim %x bytes at %08x for '%s'\n", image_size, image_base, arcpath);
        s = ENOMEM; goto out;
    }
    memory_note_claim(PHYS(image_base), image_size, MemoryLoadedProgram);
    memset((PVOID)image_base, 0, image_size);
    UCHAR sec[40 * 16];
    if (nsec > 16) { s = ENOEXEC; goto out; }
    if ((s = read_at(fd, base_off + 20 + optsz, sec, nsec * 40)) != ESUCCESS) goto out;
    for (ULONG i = 0; i < nsec; i++) {
        UCHAR *e = sec + 40 * i;
        ULONG vsize = *(ULONG *)&e[8], va = *(ULONG *)&e[12], rsize = *(ULONG *)&e[16], roff = *(ULONG *)&e[20];
        CHAR nm[9]; memcpy(nm, e, 8); nm[8] = 0;
        DBG(DBG_LOAD, "arcfw:   %-8s va %06x vsize %06x raw %06x+%06x\n", nm, va, vsize, roff, rsize);
        if (rsize && roff) {
            ULONG n = rsize < vsize || vsize == 0 ? rsize : vsize;
            if (va + n > image_size) { s = ENOEXEC; goto out; }
            if ((s = read_at(fd, roff, (PVOID)(image_base + va), n)) != ESUCCESS) goto out;
        }
    }
    cache_sweep(image_base, image_size);
    *entry_desc = (PVOID)(image_base + entry_rva);
    *low = (PVOID)image_base;
    *high = (PVOID)(image_base + image_size);
    s = ESUCCESS;
out:
    io_close(fd);
    return s;
}

VOID load_run(PVOID entry_desc, ULONG argc, PCHAR *argv, PCHAR *envp)
{
    ULONG *d = entry_desc;
    DBG(DBG_LOAD, "arcfw: entering %08x (toc %08x) with %u arguments\n", d[0], d[1], argc);
    arcfw_call_desc(entry_desc, argc, argv, envp);
}
