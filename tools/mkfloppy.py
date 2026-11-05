#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 powermac-nt-hal contributors
"""mkfloppy.py --iso <windows-nt-4.0-ppc.iso> — build the boot floppy, start to finish.

One command, one input you have to own.  It takes the one file the floppy still needs off your
CD, Microsoft's ARC veneer, patches it for the CD boot, and lays the image out with the HAL, the
ADB driver and the ARC firmware this repository builds.  `make floppy ISO=<path>` is this with
the compile in front of it.

WHAT COMES OFF YOUR CD, AND WHY IT MATTERS.  One of the eight files on the finished floppy is
Microsoft's -- the veneer, patched for the install boot -- and it is modified.  **A built floppy
image therefore cannot be redistributed.**  Nothing of theirs is stored in this repository; it is
read from the image you name on the command line, which is why this tool needs one.  See
PROVENANCE.md.  The installed system boots through this project's own firmware, src/arcfw
(docs/2026-11-05-the-arc-firmware.md), so the disk-patched veneer is gone since 5 November 2026.
SETUPLDR stays on the CD, where the veneer loads it; the Cirrus display pair stays on the CD too,
since the same day: the HAL translates the legacy VGA window a stock cirrus.sys asks for, so
Setup's own copy works and ledger row 6 is retired (docs/2026-11-05-the-vga-window.md).
"""
import argparse, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import isocat  # noqa: E402

# (member on the CD, what we call it, what it is)
NEEDED = [
    (r'\PPC\VENEER.EXE', 'veneer.exe', "the ARC firmware veneer"),
]


def run(argv, what):
    print(f'  {what}')
    r = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f'{what}: failed')
    return r.stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--iso', required=True, help='your Windows NT 4.0 PowerPC CD image')
    ap.add_argument('--out', default='build/boot-floppy.img', help='the floppy to write')
    ap.add_argument('--work', default='build/oemsrc', help='where the extracted files land')
    ap.add_argument('--hal', default='build/hal.dll')
    ap.add_argument('--adb-driver', default='build/adbport.sys')
    ap.add_argument('--arc-firmware', default='build/arcfw.exe')
    a = ap.parse_args()

    for path, what in ((a.hal, 'the HAL'), (a.adb_driver, 'the ADB driver'),
                       (a.arc_firmware, 'the ARC firmware')):
        if not os.path.exists(os.path.join(ROOT, path)):
            raise SystemExit(f'{path} is missing -- run `make` first ({what})')

    work = os.path.join(ROOT, a.work)
    os.makedirs(work, exist_ok=True)

    print(f'Taking one file off {a.iso}:')
    for member, name, what in NEEDED:
        blob = isocat.read(a.iso, member)
        with open(os.path.join(work, name), 'wb') as g:
            g.write(blob)
        print(f'  {name:12} {len(blob):8} bytes   {what}')

    # One veneer, patched for the CD boot (it carries the VrOpen patch, ledger row 4).  The
    # installed system boots through src/arcfw instead, which needs no patching.
    print('Patching the veneer for the install boot:')
    src = os.path.join(work, 'veneer.exe')
    run([sys.executable, 'tools/mkveneer.py', src, '--out', os.path.join(work, 'veneer-fd.exe'),
         '--for', 'cd'], 'for the cd: veneer-fd.exe')

    print('Laying out the floppy:')
    out = run([sys.executable, 'tools/mkbootfloppy.py',
               '--out', a.out,
               '--veneer', os.path.join(work, 'veneer-fd.exe'),
               '--arc-firmware', a.arc_firmware,
               '--hal', a.hal,
               '--adb-driver', a.adb_driver], 'mkbootfloppy')
    for line in out.splitlines():
        if 'VENEER.EXE' in line or 'ARCFW.EXE' in line or 'bytes, FAT12' in line:
            print(f'  {line.strip()}')

    print(f'\n{a.out} is ready.')
    print('Do not redistribute it: one of its files is Microsoft\'s veneer, modified.')


if __name__ == '__main__':
    main()
