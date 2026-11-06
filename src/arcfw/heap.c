/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 * heap.c -- a bump allocator for the firmware's own tables (configuration tree, strings, file
 * table).  They live in memory claimed from Open Firmware, which the memory descriptors type
 * FirmwareTemporary: NT's loader copies everything it wants out of the tree before the kernel
 * runs, and the kernel reclaims the type.  Nothing is ever
 * freed: the firmware builds its tables once. */
#include "arcfw.h"
#include "of.h"

#define HEAP_SIZE (96 * 1024)
static UCHAR *heap;
static ULONG heap_used;

PVOID zalloc_aligned(ULONG n, ULONG align)
{
    if (!heap) {
        /* Claimed from Open Firmware rather than carried in the image: pe-loader copies the
         * file flat, so bss is file bytes, and 96 KB of zeros is 192 floppy blocks a real drive
         * would read at every boot.  What is claimed leaves `available`, so the memory
         * descriptors type it FirmwareTemporary on their own. */
        heap = of_claim(NULL, HEAP_SIZE, 4096);
        if (heap == (PVOID)OF_NONE || heap == NULL) fatal("cannot claim the firmware heap");
        /* The 2.26NT ROM's claim maps what it hands out; the stock 1.1.22 ROM's does not (the
         * first store into the heap was a DSI at 0x5007, 6 November), so map it, mode 0.  Where
         * it is mapped already the call is a no-op or an error, either way ignored. */
        of_map((ULONG)heap, (ULONG)heap, HEAP_SIZE, 0);
    }
    ULONG at = (heap_used + align - 1) & ~(align - 1);
    if (at + n > HEAP_SIZE) fatal("firmware heap exhausted (%u + %u)", at, n);
    heap_used = at + n;
    memset(heap + at, 0, n);
    return heap + at;
}
PVOID zalloc(ULONG n) { return zalloc_aligned(n, 8); }
CHAR *strdup_(const CHAR *s) { CHAR *d = zalloc(strlen(s) + 1); strcpy(d, s); return d; }
