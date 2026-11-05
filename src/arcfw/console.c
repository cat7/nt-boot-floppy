/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 * console.c -- printf onto Open Firmware's stdout, which setup.of put on ttya (or the screen).
 * Original.  %s %c %d %u %x %X %p %08x %-16s and %% are what the firmware uses. */
#include "arcfw.h"
#include "of.h"

ULONG arcfw_debug;

VOID con_putc(CHAR c)
{
    if (c == '\n') { CHAR cr = '\r'; of_write(of_stdout, &cr, 1); }
    of_write(of_stdout, &c, 1);
}
VOID con_puts(const CHAR *s) { while (*s) con_putc(*s++); }

static ULONG emit_num(CHAR *buf, ULONG n, ULONG pos, ULONG v, ULONG base, BOOLEAN upper, LONG width, CHAR pad)
{
    CHAR tmp[12]; ULONG i = 0;
    do { ULONG d = v % base; tmp[i++] = (CHAR)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10); v /= base; } while (v);
    while ((LONG)i < width) tmp[i++] = pad;
    while (i) { if (pos < n) buf[pos] = tmp[--i]; else i--; pos++; }
    return pos;
}

ULONG con_vsnprintf(CHAR *buf, ULONG n, const CHAR *fmt, va_list ap)
{
    ULONG pos = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { if (pos < n) buf[pos] = *fmt; pos++; continue; }
        fmt++;
        BOOLEAN left = FALSE; CHAR pad = ' '; LONG width = 0;
        if (*fmt == '-') { left = TRUE; fmt++; }
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == 'l') fmt++;
        switch (*fmt) {
        case 's': {
            const CHAR *s = va_arg(ap, const CHAR *); if (!s) s = "(null)";
            ULONG len = strlen(s);
            if (!left) for (LONG k = len; k < width; k++) { if (pos < n) buf[pos] = ' '; pos++; }
            for (; *s; s++) { if (pos < n) buf[pos] = *s; pos++; }
            if (left) for (LONG k = len; k < width; k++) { if (pos < n) buf[pos] = ' '; pos++; }
            break; }
        case 'c': { CHAR c = (CHAR)va_arg(ap, int); if (pos < n) buf[pos] = c; pos++; break; }
        case 'd': { LONG v = va_arg(ap, LONG); if (v < 0) { if (pos < n) buf[pos] = '-'; pos++; v = -v; width--; }
                    pos = emit_num(buf, n, pos, (ULONG)v, 10, FALSE, width, pad); break; }
        case 'u': pos = emit_num(buf, n, pos, va_arg(ap, ULONG), 10, FALSE, width, pad); break;
        case 'x': pos = emit_num(buf, n, pos, va_arg(ap, ULONG), 16, FALSE, width, pad); break;
        case 'X': pos = emit_num(buf, n, pos, va_arg(ap, ULONG), 16, TRUE, width, pad); break;
        case 'p': pos = emit_num(buf, n, pos, (ULONG)va_arg(ap, PVOID), 16, FALSE, 8, '0'); break;
        case '%': if (pos < n) buf[pos] = '%'; pos++; break;
        default: if (pos < n) buf[pos] = '%'; pos++; if (pos < n) buf[pos] = *fmt; pos++; break;
        }
    }
    if (n) buf[pos < n ? pos : n - 1] = 0;
    return pos;
}

ULONG con_snprintf(CHAR *buf, ULONG n, const CHAR *fmt, ...)
{
    va_list ap; va_start(ap, fmt); ULONG r = con_vsnprintf(buf, n, fmt, ap); va_end(ap); return r;
}

VOID con_printf(const CHAR *fmt, ...)
{
    CHAR buf[512]; va_list ap;
    va_start(ap, fmt); con_vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    con_puts(buf);
}

VOID fatal(const CHAR *fmt, ...)
{
    CHAR buf[512]; va_list ap;
    va_start(ap, fmt); con_vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    con_puts("\narcfw: fatal: "); con_puts(buf); con_puts("\n");
    of_exit();
}
