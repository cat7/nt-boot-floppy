/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * io.c -- ARC Open/Close/Read/Write/Seek over Open Firmware instances.
 *
 * An ARC path names a component (config_lookup) and, after it, "partition(N)" and a file.  The
 * Open Firmware path is the component's node with Apple's disk-label argument: ":N" for a
 * partition (which gives partition-relative sectors, wall 46), nothing at all for a CD-ROM
 * (Apple's disk-label answers ":N" on an ISO with the root directory as a file, wall 22 / row
 * 4), and ":N,\path" for a file inside a partition, read through Open Firmware's own filesystem
 * packages.  Reads and seeks are byte offsets, as ARC's are.  File ids 0 and 1 are the console
 * Open Firmware gave us; the first one handed out is 2, as the veneer did. */
#include "arcfw.h"
#include "of.h"

#define MAX_FD 16
typedef struct {
    BOOLEAN open;
    OF_IHANDLE ih;
    BOOLEAN console, is_file, is_cdrom;
    ULONG pos_hi, pos_lo;
    ULONG size_hi, size_lo;        /* a file's size, when known */
    CHAR pending; BOOLEAN has_pending;
    CHAR path[160];
} FD;
static FD fds[MAX_FD];

VOID io_init(VOID)
{
    memset(fds, 0, sizeof fds);
    fds[0].open = TRUE; fds[0].console = TRUE; fds[0].ih = of_stdin; strcpy(fds[0].path, "stdin");
    fds[1].open = TRUE; fds[1].console = TRUE; fds[1].ih = of_stdout; strcpy(fds[1].path, "stdout");
}

static ARC_STATUS of_path_for(const CHAR *arcpath, CHAR *ofpath, ULONG len, BOOLEAN *is_file, BOOLEAN *is_cdrom, BOOLEAN *console)
{
    const CHAR *rest;
    PARC_NODE n = config_lookup(arcpath, &rest);
    if (!n) return ENODEV;
    *is_file = FALSE; *is_cdrom = FALSE; *console = FALSE;
    if (!n->OfPath) return ENODEV;
    ULONG partition = 0; BOOLEAN have_part = FALSE;
    const CHAR *p = rest;
    if (strncasecmp(p, "partition(", 10) == 0) { p += 10; partition = atoul(p, &p); if (*p == ')') p++; have_part = TRUE; }
    CONFIGURATION_TYPE t = n->Component.Type;
    /* the peripheral's controller is where the device lives; both share the OF path */
    if (t == SerialController || t == LinePeripheral) {
        con_snprintf(ofpath, len, "%s:0", n->OfPath);
        *console = TRUE;
        return ESUCCESS;
    }
    if (t == CdromController || (t == FloppyDiskPeripheral && n->Parent && n->Parent->Component.Type == CdromController)) *is_cdrom = TRUE;
    if (*p == 0) {
        if (*is_cdrom) con_snprintf(ofpath, len, "%s", n->OfPath);
        else con_snprintf(ofpath, len, "%s:%u", n->OfPath, have_part ? partition : 0);
        return ESUCCESS;
    }
    /* a file: through Open Firmware's filesystem packages */
    *is_file = TRUE;
    if (*is_cdrom) con_snprintf(ofpath, len, "%s:,%s", n->OfPath, p);
    else con_snprintf(ofpath, len, "%s:%u,%s", n->OfPath, partition, p);
    return ESUCCESS;
}

ARC_STATUS io_open(const CHAR *path, OPEN_MODE mode, PULONG fd)
{
    ULONG i;
    for (i = 2; i < MAX_FD && fds[i].open; i++) { }
    if (i == MAX_FD) return EMFILE;
    FD *f = &fds[i];
    memset(f, 0, sizeof *f);
    ARC_STATUS s = of_path_for(path, f->path, sizeof f->path, &f->is_file, &f->is_cdrom, &f->console);
    DBG(DBG_IO, "arcfw: Open('%s' mode %u) -> %s", path, mode, s == ESUCCESS ? f->path : "ENODEV");
    if (s != ESUCCESS) { DBG(DBG_IO, "\n"); return s; }
    f->ih = of_open(f->path);
    if (!f->ih) { DBG(DBG_IO, " (open failed)\n"); return ENOENT; }
    f->open = TRUE;
    *fd = i;
    DBG(DBG_IO, " fd %u ih %x\n", i, f->ih);
    (void)mode;
    return ESUCCESS;
}

ARC_STATUS io_close(ULONG fd)
{
    if (fd >= MAX_FD || !fds[fd].open) return EBADF;
    if (fd >= 2) { of_close(fds[fd].ih); fds[fd].open = FALSE; }
    DBG(DBG_IO, "arcfw: Close(%u)\n", fd);
    return ESUCCESS;
}

static VOID advance(FD *f, ULONG n)
{
    ULONG lo = f->pos_lo + n;
    if (lo < f->pos_lo) f->pos_hi++;
    f->pos_lo = lo;
}

ARC_STATUS io_read(ULONG fd, PVOID buf, ULONG n, PULONG count)
{
    if (fd >= MAX_FD || !fds[fd].open) return EBADF;
    FD *f = &fds[fd];
    *count = 0;
    if (f->console) {
        CHAR *out = buf; ULONG got = 0;
        if (f->has_pending && n) { out[got++] = f->pending; f->has_pending = FALSE; }
        while (got < n) {
            LONG r = of_read(f->ih, out + got, 1);
            if (r <= 0) { if (got) break; continue; }      /* the console read blocks until a key */
            got += r;
            break;                                          /* a line reader wants one at a time */
        }
        *count = got;
        return ESUCCESS;
    }
    LONG r = of_read(f->ih, buf, n);
    DBG(DBG_RW, "arcfw: Read(%u, %u) at %x:%08x -> %d\n", fd, n, f->pos_hi, f->pos_lo, r);
    if (r < 0) return EIO;
    advance(f, (ULONG)r);
    *count = (ULONG)r;
    return ESUCCESS;
}

ARC_STATUS io_write(ULONG fd, const VOID *buf, ULONG n, PULONG count)
{
    if (fd >= MAX_FD || !fds[fd].open) return EBADF;
    FD *f = &fds[fd];
    LONG r = of_write(f->ih, buf, n);
    if (r < 0) { *count = 0; return EIO; }
    if (!f->console) advance(f, (ULONG)r);
    *count = (ULONG)r;
    return ESUCCESS;
}

ARC_STATUS io_seek(ULONG fd, LARGE_INTEGER *off, SEEK_MODE mode)
{
    if (fd >= MAX_FD || !fds[fd].open) return EBADF;
    FD *f = &fds[fd];
    if (f->console) return ESUCCESS;
    ULONG hi = (ULONG)off->HighPart, lo = off->LowPart;
    if (mode == SeekRelative) { ULONG nlo = f->pos_lo + lo; hi += f->pos_hi + (nlo < lo ? 1 : 0); lo = nlo; }
    DBG(DBG_RW, "arcfw: Seek(%u) to %x:%08x\n", fd, hi, lo);
    if (of_seek(f->ih, hi, lo) >= 0) { f->pos_hi = hi; f->pos_lo = lo; return ESUCCESS; }
    if (!f->is_file || hi) return EIO;
    /* Open Firmware's iso-9660-files package has no seek (its FAT package has): measured
     * 5 November, Open of \\PPC\\SETUPLDR on the CD succeeding and the first Seek answering
     * -1.  So a file that cannot seek is read forward to the offset, and reopened first for an
     * offset behind the current one; an image's header is read three times that way, which is
     * nothing against the file. */
    if (lo < f->pos_lo || f->pos_hi) {
        of_close(f->ih);
        f->ih = of_open(f->path);
        if (!f->ih) { f->open = FALSE; return EIO; }
        f->pos_hi = 0; f->pos_lo = 0;
    }
    static UCHAR skip[4096] __attribute__((aligned(16)));
    while (f->pos_lo < lo) {
        ULONG n = lo - f->pos_lo; if (n > sizeof skip) n = sizeof skip;
        LONG r = of_read(f->ih, skip, n);
        if (r <= 0) return EIO;
        f->pos_lo += (ULONG)r;
    }
    DBG(DBG_RW, "arcfw: Seek(%u) reached %08x by reading\n", fd, f->pos_lo);
    return ESUCCESS;
}

ARC_STATUS io_get_read_status(ULONG fd)
{
    if (fd >= MAX_FD || !fds[fd].open) return EBADF;
    FD *f = &fds[fd];
    if (!f->console) return ESUCCESS;
    if (f->has_pending) return ESUCCESS;
    LONG r = of_read(f->ih, &f->pending, 1);              /* Open Firmware's console read does not block */
    if (r == 1) { f->has_pending = TRUE; return ESUCCESS; }
    return EAGAIN;
}

ARC_STATUS io_get_file_information(ULONG fd, PFILE_INFORMATION fi)
{
    if (fd >= MAX_FD || !fds[fd].open) return EBADF;
    FD *f = &fds[fd];
    memset(fi, 0, sizeof *fi);
    fi->CurrentPosition.LowPart = f->pos_lo; fi->CurrentPosition.HighPart = (LONG)f->pos_hi;
    fi->EndingAddress.LowPart = f->size_lo; fi->EndingAddress.HighPart = (LONG)f->size_hi;
    fi->Type = f->console ? SerialController : f->is_cdrom ? CdromController : DiskController;
    fi->Attributes = f->is_file ? 0 : 0;
    return ESUCCESS;
}

/* The whole of a file at once, through the instance's `load` method -- what Open Firmware's own
 * `load` command does.  Its iso-9660-files package answers -1 to read and seek (measured 5
 * November on \\PPC\\SETUPLDR), and `load` is the one way it gives a file up.  The buffer is a
 * megabyte claimed at 48 MB, below the floppy's staging areas and above anything NT's loaders
 * link at; it stays claimed, which the memory descriptors report as firmware-temporary. */
#define WHOLE_PHYS 0x03000000u
#define WHOLE_SIZE 0x00100000u
static BOOLEAN whole_claimed;
ARC_STATUS io_load_whole(ULONG fd, PVOID *buf, PULONG size)
{
    if (fd >= MAX_FD || !fds[fd].open || !fds[fd].is_file) return EBADF;
    if (!whole_claimed) {
        if (of_claim_real(WHOLE_PHYS, WHOLE_SIZE) < 0) return ENOMEM;
        whole_claimed = TRUE;
    }
    ULONG in[1] = { WHOLE_PHYS }, out[1];
    if (of_call_method("load", fds[fd].ih, 1, 1, in, out) || out[0] == 0 || out[0] > WHOLE_SIZE) {
        DBG(DBG_IO, "arcfw: load of fd %u through the instance's load method failed\n", fd);
        return EIO;
    }
    DBG(DBG_IO, "arcfw: fd %u loaded whole: %u bytes at %08x\n", fd, out[0], WHOLE_PHYS);
    *buf = (PVOID)WHOLE_PHYS; *size = out[0];
    return ESUCCESS;
}
