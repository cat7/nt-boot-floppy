/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 * string.c -- the few C library routines a freestanding image needs.  Original. */
#include "arcfw.h"

ULONG strlen(const CHAR *s) { ULONG n = 0; while (s[n]) n++; return n; }
LONG strcmp(const CHAR *a, const CHAR *b) { while (*a && *a == *b) { a++; b++; } return (UCHAR)*a - (UCHAR)*b; }
LONG strncmp(const CHAR *a, const CHAR *b, ULONG n) { while (n && *a && *a == *b) { a++; b++; n--; } return n ? (UCHAR)*a - (UCHAR)*b : 0; }
CHAR toupper(CHAR c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
LONG strcasecmp(const CHAR *a, const CHAR *b) { while (*a && toupper(*a) == toupper(*b)) { a++; b++; } return (UCHAR)toupper(*a) - (UCHAR)toupper(*b); }
LONG strncasecmp(const CHAR *a, const CHAR *b, ULONG n) { while (n && *a && toupper(*a) == toupper(*b)) { a++; b++; n--; } return n ? (UCHAR)toupper(*a) - (UCHAR)toupper(*b) : 0; }
CHAR *strcpy(CHAR *d, const CHAR *s) { CHAR *r = d; while ((*d++ = *s++)) { } return r; }
CHAR *strcat(CHAR *d, const CHAR *s) { strcpy(d + strlen(d), s); return d; }
CHAR *strchr(const CHAR *s, CHAR c) { for (; *s; s++) if (*s == c) return (CHAR *)s; return c ? NULL : (CHAR *)s; }
VOID *memcpy(VOID *d, const VOID *s, ULONG n) { UCHAR *dd = d; const UCHAR *ss = s; while (n--) *dd++ = *ss++; return d; }
VOID *memset(VOID *d, int c, ULONG n) { volatile UCHAR *dd = d; while (n--) *dd++ = (UCHAR)c; return d; }
LONG memcmp(const VOID *a, const VOID *b, ULONG n) { const UCHAR *x = a, *y = b; while (n--) { if (*x != *y) return *x - *y; x++; y++; } return 0; }
ULONG atoul(const CHAR *s, const CHAR **end)
{
    ULONG v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (ULONG)(*s++ - '0');
    if (end) *end = s;
    return v;
}
