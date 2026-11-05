/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * env.c -- the ARC environment, which on this firmware is Open Firmware's /options node: the
 * boot scripts put the ten boot variables there as properties for the boot (E24, E26 in the
 * floppy note), and `setenv` at the prompt puts them in NVRAM.  ARC names are case-insensitive
 * and the loader asks in mixed case ('OsLoader', 'OSLoadFilename'); the scripts write them in
 * upper case, so the lookup upper-cases.  Original. */
#include "arcfw.h"
#include "of.h"

static OF_PHANDLE options;
static CHAR value_buf[256];

static VOID upper_name(const CHAR *name, CHAR *out, ULONG n)
{
    ULONG i = 0;
    while (name[i] && i + 1 < n) { out[i] = toupper(name[i]); i++; }
    out[i] = 0;
}

const CHAR *env_lookup(const CHAR *name)
{
    CHAR key[64];
    if (!options) options = of_finddevice("/options");
    if (options == OF_NONE || !name) return NULL;
    upper_name(name, key, sizeof key);
    LONG n = of_getprop(options, key, value_buf, sizeof value_buf - 1);
    if (n < 0) return NULL;
    value_buf[n] = 0;
    /* a property written by setenv carries no terminator; one written by the script may */
    return value_buf;
}

ARC_STATUS env_get(const CHAR *name, USHORT len, CHAR *buf)
{
    const CHAR *v = env_lookup(name);
    DBG(DBG_ENV, "arcfw: GetEnvironmentVariable('%s') -> %s\n", name, v ? v : "ENOENT");
    if (!v) return ENOENT;
    ULONG vl = strlen(v);
    if (vl + 1 > len) return ENOMEM;
    strcpy(buf, v);
    return ESUCCESS;
}

ARC_STATUS env_set(const CHAR *name, const CHAR *value)
{
    CHAR key[64];
    if (!options) options = of_finddevice("/options");
    if (options == OF_NONE || !name || !value) return EINVAL;
    upper_name(name, key, sizeof key);
    DBG(DBG_ENV, "arcfw: SetEnvironmentVariable('%s', '%s')\n", key, value);
    return of_setprop(options, key, value, strlen(value) + 1) < 0 ? EIO : ESUCCESS;
}
