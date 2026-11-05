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

/* A file on a CD, found by this firmware itself: ISO 9660 read through the raw device, which
 * Open Firmware's SCSI package reads and seeks at byte offsets (SETUPLDR reads the CD that way
 * too, E3).  Its iso-9660-files package opened \\PPC\\SETUPLDR and then answered -1 to read and
 * seek, and its load method threw (measured 5 November), so the package is not used.  The
 * primary volume descriptor is at sector 16; a directory record is: length, ext-attr length,
 * extent LBA (both-endian, 8 bytes), size (both-endian, 8 bytes), 7 bytes of date, flags, unit
 * size, gap, volume sequence (4), name length, name; names are upper case with a ";1" version. */
#define ISO_SECTOR 2048
static ULONG iso_le32(const UCHAR *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((ULONG)p[3] << 24); }

static ARC_STATUS iso_read_sector(ULONG fd, ULONG lba, UCHAR *buf)
{
    LARGE_INTEGER li; li.HighPart = 0; li.LowPart = lba * ISO_SECTOR;
    ULONG got;
    if (io_seek(fd, &li, SeekAbsolute) != ESUCCESS) return EIO;
    if (io_read(fd, buf, ISO_SECTOR, &got) != ESUCCESS || got != ISO_SECTOR) return EIO;
    return ESUCCESS;
}

static BOOLEAN iso_name_is(const UCHAR *name, ULONG len, const CHAR *want)
{
    ULONG i = 0;
    for (; i < len && want[i]; i++) if (toupper(name[i]) != toupper(want[i])) return FALSE;
    if (want[i]) return FALSE;
    return i == len || (name[i] == ';') || (name[i] == '.' && i + 1 < len && name[i + 1] == ';') || (name[i] == '.' && i + 1 == len);
}

/* look `want` (one path component) up in the directory whose extent is lba/size */
static ARC_STATUS iso_lookup(ULONG fd, ULONG lba, ULONG size, const CHAR *want, PULONG out_lba, PULONG out_size, BOOLEAN *is_dir)
{
    static UCHAR sec[ISO_SECTOR] __attribute__((aligned(16)));
    for (ULONG off = 0; off < size; off += ISO_SECTOR) {
        if (iso_read_sector(fd, lba + off / ISO_SECTOR, sec) != ESUCCESS) return EIO;
        ULONG at = 0;
        while (at + 33 <= ISO_SECTOR) {
            ULONG len = sec[at];
            if (len == 0) break;                         /* the rest of the sector is padding */
            ULONG nlen = sec[at + 32];
            if (nlen > 1 && iso_name_is(sec + at + 33, nlen, want)) {
                *out_lba = iso_le32(sec + at + 2); *out_size = iso_le32(sec + at + 10);
                *is_dir = (sec[at + 25] & 2) != 0;
                return ESUCCESS;
            }
            at += len;
        }
    }
    return ENOENT;
}

/* Open the device an ARC path names, raw, and find the file after it: the fd, the file's byte
 * offset on the device and its size.  The ARC path's file part uses backslashes. */
ARC_STATUS io_open_iso_file(const CHAR *arcpath, PULONG fd, PULONG offset, PULONG size)
{
    CHAR dev[160]; ULONG i = 0;
    const CHAR *file = arcpath;
    while (*file && *file != '\\') file++;
    for (; i + 1 < sizeof dev && arcpath + i < file; i++) dev[i] = arcpath[i];
    dev[i] = 0;
    if (!*file) return ENOENT;
    ARC_STATUS s = io_open(dev, ArcOpenReadOnly, fd);
    if (s != ESUCCESS) return s;
    static UCHAR pvd[ISO_SECTOR] __attribute__((aligned(16)));
    if (iso_read_sector(*fd, 16, pvd) != ESUCCESS || memcmp(pvd + 1, "CD001", 5) != 0) { io_close(*fd); return EIO; }
    ULONG lba = iso_le32(pvd + 156 + 2), sz = iso_le32(pvd + 156 + 10); BOOLEAN dir = TRUE;
    const CHAR *p = file;
    while (*p) {
        while (*p == '\\') p++;
        if (!*p) break;
        CHAR part[64]; ULONG n = 0;
        while (*p && *p != '\\' && n + 1 < sizeof part) part[n++] = *p++;
        part[n] = 0;
        if (!dir) { io_close(*fd); return ENOTDIR; }
        if (iso_lookup(*fd, lba, sz, part, &lba, &sz, &dir) != ESUCCESS) {
            DBG(DBG_IO, "arcfw: '%s' not on the CD\n", part);
            io_close(*fd); return ENOENT;
        }
    }
    if (dir) { io_close(*fd); return EISDIR; }
    *offset = lba * ISO_SECTOR; *size = sz;
    DBG(DBG_IO, "arcfw: '%s' on the CD: lba %u, %u bytes\n", file, lba, sz);
    return ESUCCESS;
}
