/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 * heap.c -- a bump allocator for the firmware's own tables (configuration tree, strings, file
 * table).  They live in the image's bss, which the memory descriptors type FirmwarePermanent, so
 * the kernel never reuses them while the firmware's data might still be read.  Nothing is ever
 * freed: the firmware builds its tables once. */
#include "arcfw.h"

static UCHAR heap[96 * 1024] __attribute__((aligned(16)));
static ULONG heap_used;

PVOID zalloc_aligned(ULONG n, ULONG align)
{
    ULONG at = (heap_used + align - 1) & ~(align - 1);
    if (at + n > sizeof heap) fatal("firmware heap exhausted (%u + %u)", at, n);
    heap_used = at + n;
    memset(heap + at, 0, n);
    return heap + at;
}
PVOID zalloc(ULONG n) { return zalloc_aligned(n, 8); }
CHAR *strdup_(const CHAR *s) { CHAR *d = zalloc(strlen(s) + 1); strcpy(d, s); return d; }
