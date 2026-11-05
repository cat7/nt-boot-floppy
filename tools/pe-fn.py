#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 powermac-nt-hal contributors
"""pe-fn.py <pe> <export-name | 0x<descriptor-va>> [ninsns]  — disassemble one routine of an NT
PowerPC PE by name.

NT PowerPC exports and entry points name {entry, toc} descriptors, not code.  This resolves the
descriptor -- an export by name through the export table, or a descriptor address given in hex
(the entry-point RVA of a .sys, say) -- and runs pe-dis.py from the entry with that TOC, which
is what makes TOC-relative loads annotate.  `ntoskrnl.exe IoCreateDevice` is how the DEVICE_OBJECT
layout in drivers/adbport/adbport.h was read; see docs/2026-11-05-adbport-layouts-verified.md.
"""
import os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from('<I', d, opt + 28)[0]
    secs = []
    so = opt + optsz
    for i in range(nsec):
        vs, va, rs, rp = struct.unpack_from('<IIII', d, so + 40 * i + 8)
        secs.append((base + va, vs, rp, rs))

    def rd(addr, k):
        for va, vs, rp, rs in secs:
            if va <= addr < va + max(vs, rs):
                o = addr - va
                return d[rp + o:rp + o + k]
        raise SystemExit(f'{addr:#x} is not in the image')
    return d, opt, base, rd


def export_descriptor(d, opt, base, rd, name):
    exp_rva = struct.unpack_from('<I', d, opt + 96)[0]
    if not exp_rva:
        raise SystemExit('no export table')
    e = rd(base + exp_rva, 40)
    nfunc, nnames, addr_rva, name_rva, ord_rva = struct.unpack_from('<IIIII', e, 20)
    for i in range(nnames):
        nm_rva = struct.unpack_from('<I', rd(base + name_rva + 4 * i, 4), 0)[0]
        nm = rd(base + nm_rva, 96).split(b'\0')[0].decode()
        if nm == name:
            idx = struct.unpack_from('<H', rd(base + ord_rva + 2 * i, 2), 0)[0]
            return base + struct.unpack_from('<I', rd(base + addr_rva + 4 * idx, 4), 0)[0]
    raise SystemExit(f'no export named {name}')


def disassemble(path, what, n=80):
    """The disassembly of `what` (an export name or 0x<descriptor va>) as text."""
    d, opt, base, rd = load(path)
    desc = int(what, 16) if what.lower().startswith('0x') else export_descriptor(d, opt, base, rd, what)
    entry, toc = struct.unpack_from('<II', rd(desc, 8), 0)
    env = dict(os.environ, TOC=f'{toc:x}')
    r = subprocess.run([sys.executable, os.path.join(HERE, 'pe-dis.py'), path, f'{entry:x}',
                        f'{entry + 4 * n:x}'], env=env, capture_output=True, text=True)
    head = f'# {what}: descriptor {desc:#x} -> entry {entry:#x} toc {toc:#x}\n'
    body = '\n'.join(l for l in r.stdout.splitlines() if not l.startswith('# base'))
    return head + body + '\n' + r.stderr


if __name__ == '__main__':
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    sys.stdout.write(disassemble(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 80))
