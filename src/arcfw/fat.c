/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * fat.c -- files on a FAT partition, found by this firmware itself.
 *
 * WHY.  Open Firmware has a fat-files package and the firmware used it: "sd@0,0:1,\os\winnt40\
 * osloader.exe" opens on the 2.26NT ROM.  The production Network Server ROM (Open Firmware
 * 1.1.22) carries a fat-files too, and it descends ONE directory: `dir sd@0,0:1,\OS\` lists
 * WINNT40, `dir sd@0,0:1,\OS\WINNT40\` is "can't OPEN the DIR device", and open-dev of the
 * loader answers 0 (measured 6 November, le-prod8).  NT keeps everything two levels down and
 * deeper.  So, as for the CD (io.c, ISO 9660), the firmware reads the disk raw and walks the
 * filesystem itself: the MBR for the partition, the BPB, the FAT, the directories.  That makes
 * every file NT's loaders open -- OSLOADER, the kernel, the HAL, the drivers, the SYSTEM hive --
 * independent of whichever ROM is in the machine.
 *
 * WHAT.  FAT12, FAT16 and FAT32, 8.3 names (long-name entries are skipped; NT 4's system
 * partition files are 8.3), read-only.  The disk is opened whole (":0" is the whole disk in
 * Apple's disk-label) and read at byte offsets through Open Firmware's SCSI package.  ARC
 * numbers partitions as NT does: the MBR's non-empty primary entries in order, from 1.  A
 * disk without an MBR whose first sector is itself a BPB (a floppy, a superfloppy) is
 * partition 1.  Reads merge runs of contiguous clusters into one Open Firmware read, so a
 * 500 KB loader on 1 KB clusters is a handful of calls, not five hundred.  Original. */
#include "arcfw.h"
#include "of.h"

#define SECTOR 512

/* volatile: see iso_le32 in io.c -- a fused unaligned load is an alignment exception on the 604 */
static ULONG le16(const UCHAR *p) { volatile const UCHAR *q = p; return q[0] | (q[1] << 8); }
static ULONG le32(const UCHAR *p) { volatile const UCHAR *q = p; return q[0] | (q[1] << 8) | (q[2] << 16) | ((ULONG)q[3] << 24); }

/* read n bytes at byte offset (sector * 512 + off) of the device */
static ARC_STATUS dev_read(FAT_FILE *ff, ULONG sector, ULONG off, VOID *buf, ULONG n)
{
    ULONGLONG pos = (ULONGLONG)sector * SECTOR + off;
    if (of_seek(ff->dev, (ULONG)(pos >> 32), (ULONG)pos) < 0) return EIO;
    UCHAR *out = buf;
    while (n) {
        LONG r = of_read(ff->dev, out, n);
        if (r <= 0) return EIO;
        out += r; n -= (ULONG)r;
    }
    return ESUCCESS;
}

/* one sector of the partition, cached (the FAT and directories are read a sector at a time) */
static ARC_STATUS part_sector(FAT_FILE *ff, ULONG sec, UCHAR **out)
{
    if (ff->cache_sec != sec) {
        ARC_STATUS s = dev_read(ff, ff->part_lba + sec, 0, ff->cache, SECTOR);
        if (s != ESUCCESS) { ff->cache_sec = 0xFFFFFFFF; return s; }
        ff->cache_sec = sec;
    }
    *out = ff->cache;
    return ESUCCESS;
}

static ARC_STATUS fat_byte(FAT_FILE *ff, ULONG off, ULONG *v)
{
    UCHAR *sec;
    ARC_STATUS s = part_sector(ff, ff->fat_sec + off / ff->bps, &sec);
    if (s != ESUCCESS) return s;
    *v = sec[off % ff->bps];
    return ESUCCESS;
}

/* the cluster after `clus` in its chain; 0 at the end */
static ARC_STATUS fat_next(FAT_FILE *ff, ULONG clus, ULONG *next)
{
    ULONG b0, b1, b2, b3, v;
    ARC_STATUS s;
    if (ff->bits == 12) {
        ULONG off = clus + clus / 2;
        if ((s = fat_byte(ff, off, &b0)) || (s = fat_byte(ff, off + 1, &b1))) return s;
        v = b0 | (b1 << 8);
        v = (clus & 1) ? v >> 4 : v & 0xFFF;
        *next = v >= 0xFF8 ? 0 : v;
    } else if (ff->bits == 16) {
        ULONG off = clus * 2;
        if ((s = fat_byte(ff, off, &b0)) || (s = fat_byte(ff, off + 1, &b1))) return s;
        v = b0 | (b1 << 8);
        *next = v >= 0xFFF8 ? 0 : v;
    } else {
        ULONG off = clus * 4;
        if ((s = fat_byte(ff, off, &b0)) || (s = fat_byte(ff, off + 1, &b1)) ||
            (s = fat_byte(ff, off + 2, &b2)) || (s = fat_byte(ff, off + 3, &b3))) return s;
        v = (b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)) & 0x0FFFFFFF;
        *next = v >= 0x0FFFFFF8 ? 0 : v;
    }
    if (*next && (*next < 2 || *next >= ff->nclus)) *next = 0;   /* a damaged chain ends here */
    return ESUCCESS;
}

static ULONG cluster_sector(FAT_FILE *ff, ULONG clus) { return ff->data_sec + (clus - 2) * ff->spc; }

/* `want`, one path component, as the 11-byte padded 8.3 name a directory entry carries */
static BOOLEAN name83(const CHAR *want, UCHAR out[11])
{
    ULONG i, n = 0;
    memset(out, ' ', 11);
    for (i = 0; want[i] && want[i] != '.'; i++) { if (n >= 8) return FALSE; out[n++] = (UCHAR)toupper(want[i]); }
    if (want[i] == '.') {
        n = 8;
        for (i++; want[i]; i++) { if (want[i] == '.' || n >= 11) return FALSE; out[n++] = (UCHAR)toupper(want[i]); }
    }
    return TRUE;
}

/* look one component up in a directory: the fixed root (FAT12/16, root_clus 0) or a chain */
static ARC_STATUS dir_lookup(FAT_FILE *ff, ULONG dir_clus, const CHAR *want, PULONG clus, PULONG size, BOOLEAN *is_dir)
{
    UCHAR nm[11];
    if (!name83(want, nm)) return ENOENT;
    ULONG sec = dir_clus ? cluster_sector(ff, dir_clus) : ff->root_sec;
    ULONG left = dir_clus ? ff->spc : ff->root_secs;
    for (;;) {
        for (; left; left--, sec++) {
            UCHAR *d;
            ARC_STATUS s = part_sector(ff, sec, &d);
            if (s != ESUCCESS) return s;
            for (ULONG at = 0; at + 32 <= ff->bps; at += 32) {
                UCHAR *e = d + at;
                if (e[0] == 0) return ENOENT;                   /* the end of the directory */
                if (e[0] == 0xE5 || (e[11] & 0x08)) continue;   /* deleted, a label or a long-name entry */
                if (memcmp(e, nm, 11) != 0) continue;
                *clus = le16(e + 26) | (ff->bits == 32 ? le16(e + 20) << 16 : 0);
                *size = le32(e + 28);
                *is_dir = (e[11] & 0x10) != 0;
                return ESUCCESS;
            }
        }
        if (!dir_clus) return ENOENT;                            /* the fixed root has no chain */
        ARC_STATUS s = fat_next(ff, dir_clus, &dir_clus);
        if (s != ESUCCESS) return s;
        if (!dir_clus) return ENOENT;
        sec = cluster_sector(ff, dir_clus); left = ff->spc;
    }
}

/* Find partition `part` (ARC numbering) of the disk open on ff->dev: its first sector in
 * ff->part_lba.  A first sector that is a BPB rather than an MBR is partition 1 itself. */
static ARC_STATUS find_partition(FAT_FILE *ff, ULONG part)
{
    UCHAR *s0;
    ff->part_lba = 0; ff->cache_sec = 0xFFFFFFFF;
    ARC_STATUS s = part_sector(ff, 0, &s0);
    if (s != ESUCCESS) return s;
    if (s0[510] != 0x55 || s0[511] != 0xAA) return ENODEV;
    /* a BPB: a jump, a plausible sector size, and 1, 2 or 4 sectors per cluster */
    BOOLEAN bpb = (s0[0] == 0xEB || s0[0] == 0xE9) && le16(s0 + 11) >= 512 && le16(s0 + 11) <= 4096 && s0[13] && !(s0[13] & (s0[13] - 1));
    if (bpb) return part <= 1 ? ESUCCESS : ENODEV;
    ULONG n = 0;
    for (ULONG i = 0; i < 4; i++) {
        UCHAR *e = s0 + 0x1BE + 16 * i;
        if (e[4] == 0 || e[4] == 0x05 || e[4] == 0x0F) continue;    /* empty, or an extended container */
        if (++n == part) { ff->part_lba = le32(e + 8); ff->cache_sec = 0xFFFFFFFF; return ESUCCESS; }
    }
    return ENODEV;
}

/* Open the disk `devpath` (an Open Firmware path, whole) and find `file` (backslash
 * separated) on partition `part`.  Fills ff; the device stays open in ff->dev. */
ARC_STATUS fat_open(const CHAR *devpath, ULONG part, const CHAR *file, FAT_FILE *ff)
{
    memset(ff, 0, sizeof *ff);
    ff->dev = of_open(devpath);
    if (!ff->dev) return ENODEV;
    ARC_STATUS s = find_partition(ff, part);
    if (s != ESUCCESS) goto fail;
    UCHAR *b;
    if ((s = part_sector(ff, 0, &b)) != ESUCCESS) goto fail;
    ff->bps = le16(b + 11); ff->spc = b[13];
    ULONG reserved = le16(b + 14), nfats = b[16], root_ents = le16(b + 17);
    ULONG total = le16(b + 19) ? le16(b + 19) : le32(b + 32);
    ULONG fatsz = le16(b + 22) ? le16(b + 22) : le32(b + 36);
    if (ff->bps != SECTOR || !ff->spc || !nfats || !fatsz || !total) { s = EIO; goto fail; }   /* only 512-byte sectors here */
    ff->fat_sec = reserved;
    ff->root_sec = reserved + nfats * fatsz;
    ff->root_secs = (root_ents * 32 + ff->bps - 1) / ff->bps;
    ff->data_sec = ff->root_sec + ff->root_secs;
    ff->nclus = (total - ff->data_sec) / ff->spc + 2;
    if (le16(b + 22) == 0) { ff->bits = 32; ff->root_clus = le32(b + 44); }
    else ff->bits = ff->nclus < 4087 ? 12 : 16;
    DBG(DBG_IO, "arcfw: FAT%u at sector %u: %u/cluster, FAT @%u, root @%u, data @%u, %u clusters\n",
        ff->bits, ff->part_lba, ff->spc, ff->fat_sec, ff->root_sec, ff->data_sec, ff->nclus - 2);
    ULONG dir = ff->root_clus, clus = 0, size = 0; BOOLEAN is_dir = TRUE;
    const CHAR *p = file;
    while (*p) {
        while (*p == '\\') p++;
        if (!*p) break;
        CHAR part_name[64]; ULONG n = 0;
        while (*p && *p != '\\' && n + 1 < sizeof part_name) part_name[n++] = *p++;
        part_name[n] = 0;
        if (!is_dir) { s = ENOTDIR; goto fail; }
        if ((s = dir_lookup(ff, dir, part_name, &clus, &size, &is_dir)) != ESUCCESS) {
            DBG(DBG_IO, "arcfw: '%s' not on partition %u\n", part_name, part);
            goto fail;
        }
        dir = clus;
    }
    if (is_dir) { s = EISDIR; goto fail; }
    ff->first = clus; ff->size = size;
    ff->cur_idx = 0; ff->cur_clus = clus;
    DBG(DBG_IO, "arcfw: '%s' on partition %u: cluster %u, %u bytes\n", file, part, clus, size);
    return ESUCCESS;
fail:
    of_close(ff->dev); ff->dev = 0;
    return s;
}

VOID fat_close(FAT_FILE *ff)
{
    if (ff->dev) of_close(ff->dev);
    ff->dev = 0;
}

/* read up to n bytes at byte position pos; *got is what was read (short at the end) */
ARC_STATUS fat_read(FAT_FILE *ff, ULONG pos, VOID *buf, ULONG n, PULONG got)
{
    ULONG cbytes = ff->spc * ff->bps;
    UCHAR *out = buf;
    *got = 0;
    if (pos >= ff->size) return ESUCCESS;
    if (n > ff->size - pos) n = ff->size - pos;
    while (n) {
        ULONG idx = pos / cbytes, off = pos % cbytes;
        if (idx < ff->cur_idx) { ff->cur_idx = 0; ff->cur_clus = ff->first; }
        while (ff->cur_idx < idx) {
            ARC_STATUS s = fat_next(ff, ff->cur_clus, &ff->cur_clus);
            if (s != ESUCCESS) return s;
            if (!ff->cur_clus) return EIO;                 /* the chain is shorter than the size */
            ff->cur_idx++;
        }
        /* this cluster, and every contiguous one after it that the read still needs */
        ULONG first = ff->cur_clus, run = 1;
        ULONG avail = cbytes - off;
        while (avail < n) {
            ULONG next;
            ARC_STATUS s = fat_next(ff, ff->cur_clus, &next);
            if (s != ESUCCESS) return s;
            if (next != ff->cur_clus + 1) break;
            ff->cur_clus = next; ff->cur_idx++; run++;
            avail += cbytes;
        }
        ULONG chunk = avail < n ? avail : n;
        ARC_STATUS s = dev_read(ff, ff->part_lba + cluster_sector(ff, first), off, out, chunk);
        if (s != ESUCCESS) return s;
        out += chunk; pos += chunk; n -= chunk; *got += chunk;
        (void)run;
    }
    return ESUCCESS;
}
