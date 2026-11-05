#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 powermac-nt-hal contributors
"""check-nt-layouts.py --kernel ntoskrnl.exe [--kbdclass kbdclass.sys] [--mouclass mouclass.sys]
                       [--i8042prt i8042prt.sys] [--floppy floppy.sys]

Checks the NT structure layouts drivers/adbport/adbport.h declares against the code of a shipped
kernel and Microsoft's own class and port drivers -- the files of an installed system, which this
repository does not carry.  Each check is an instruction pattern the layout forces on the code:
IoInitializeIrp placing the first stack location at irp + 0x70 + StackCount * 0x24 says the IRP
is 0x70 bytes and a stack location 0x24, whatever the build.  A pattern that is not found is
reported, not asserted: run it against a kernel other than build 1381 before trusting the driver
on it.  docs/2026-11-05-adbport-layouts-verified.md records what each pattern proves and where
it was first found.

The binaries never leave your disk: this reads them and prints a table.
"""
import argparse, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pe_fn  # noqa: E402  (pe-fn.py, imported under its module name below)


def text_of(path, what, n):
    return pe_fn.disassemble(path, what, n)


def section_text(path, toc, start, end):
    env = dict(os.environ, TOC=toc)
    r = subprocess.run([sys.executable, os.path.join(HERE, 'pe-dis.py'), path, start, end],
                       env=env, capture_output=True, text=True)
    return r.stdout


def has(text, *patterns):
    """Every pattern (a regex over the disassembly text) occurs."""
    return all(re.search(p, text, re.M) for p in patterns)


KERNEL_CHECKS = [
    # (export, instructions to read, description, patterns)
    ('IoInitializeIrp', 120, 'IRP is 0x70 bytes, a stack location 0x24, CurrentStackLocation at 0x60',
     [r'mulli +r\d+,r\d+,0x24', r'addi +r\d+,r\d+,0x70', r'stw +r\d+,96\(r\d+\)']),
    ('IoInitializeIrp', 120, 'IRP.Type 6 at 0, StackCount at 0x22, CurrentLocation at 0x23',
     [r'li +r\d+,0x6\n.*sth +r\d+,0\(r\d+\)', r'stb +r\d+,34\(r\d+\)', r'stb +r\d+,35\(r\d+\)']),
    ('IoCreateDevice', 700, 'DEVICE_OBJECT is 0xB8 bytes; DeviceExtension at 0x28 follows it; StackSize at 0x30',
     [r'addi +(r\d+),r\d+,0xb8\n.*stw +\1,40\(r\d+\)', r'stb +r\d+,48\(r\d+\)']),
    ('IoCreateDevice', 700, 'DEVICE_OBJECT.Type 3 at 0, Flags at 0x1C (DO_DEVICE_INITIALIZING 0x80), DeviceType at 0x2C, Characteristics at 0x20',
     [r'li +r\d+,0x3\n.*sth +r\d+,0\(r\d+\)', r'li +r\d+,0x80\n.*stw +r\d+,28\(r\d+\)',
      r'stw +r\d+,44\(r\d+\)', r'stw +r\d+,32\(r\d+\)']),
    ('IoBuildDeviceIoControlRequest', 260, 'IO_STACK_LOCATION: MajorFunction at 0, Output/InputBufferLength at 4/8, IoControlCode at 0xC, Type3InputBuffer at 0x10',
     [r'lwz +(r\d+),96\(r\d+\)\n.*addi +r\d+,\1,-0x24', r'li +r\d+,0xe\n.*stb +r\d+,-36\(r\d+\)',
      r'stw +r\d+,4\(r\d+\)', r'stw +r\d+,8\(r\d+\)', r'stw +r\d+,12\(r\d+\)', r'stw +r\d+,16\(r\d+\)']),
    ('IoBuildDeviceIoControlRequest', 260, 'IRP: MdlAddress at 4, Flags at 8, SystemBuffer at 0xC, UserIosb/UserEvent at 0x28/0x2C, UserBuffer at 0x3C',
     [r'IoAllocateMdl\n(.*\n){1,4}.*stw +r3,4\(r\d+\)', r'li +r\d+,0x30\n.*stw +r\d+,8\(r\d+\)',
      r'stw +r\d+,12\(r\d+\)', r'stw +r\d+,40\(r\d+\)\n.*stw +r\d+,44\(r\d+\)', r'stw +r\d+,60\(r\d+\)']),
    ('IofCompleteRequest', 300, 'completion walks StackCount/CurrentLocation (0x22/0x23) and CurrentStackLocation (0x60) by 0x24; IoStatus at 0x18; CompletionRoutine/Context at 0x1C/0x20',
     [r'lbz +r\d+,34\(r\d+\)', r'lbz +r\d+,35\(r\d+\)', r'lwz +(r\d+),96\(r\d+\)\n(.*\n){0,3}.*addi +\1,\1,0x24',
      r'addi +r\d+,r\d+,0x18', r'lwz +r\d+,32\(r\d+\)\n.*lwz +r\d+,28\(r\d+\)']),
    ('MmMapLockedPages', 200, 'MDL: MdlFlags at 6 (MDL_MAPPED_TO_SYSTEM_VA = 1), MappedSystemVa at 0xC, StartVa/ByteCount/ByteOffset at 0x10/0x14/0x18',
     [r'stw +r\d+,12\(r\d+\)', r'ori +(r\d+),\1,0x1\n(.*\n){0,3}.*sth +\1,6\(r\d+\)',
      r'lwz +r\d+,16\(r\d+\)', r'lwz +r\d+,24\(r\d+\)', r'lwz +r\d+,20\(r\d+\)']),
]

# Drivers: whole sections (TOC from the entry descriptor), patterns over all their code.
DRIVER_CHECKS = {
    'kbdclass': [
        ('KEYBOARD_INPUT_DATA is 12 bytes (the input queue is stepped by 0xC)', [r'divwu +r\d+,r\d+,(r\d+)', r'mulli +r\d+,r\d+,0xc']),
        ('IOCTL_INTERNAL_KEYBOARD_CONNECT is 0xB0203 with an 8-byte CONNECT_DATA', [r'lis +(r\d+),0xb\n.*ori +\1,\1,0x203\n.*li +r6,0x8']),
        ('DRIVER_OBJECT.MajorFunction at 0x38: CREATE/CLOSE/READ/FLUSH/DEVICE_CONTROL/CLEANUP at 0x38/0x40/0x44/0x5C/0x70/0x80, DriverStartIo at 0x30',
         [r'stw +r\d+,56\((r\d+)\)\n.*stw +r\d+,48\(\1\)\n.*stw +r\d+,64\(\1\)\n.*stw +r\d+,68\(\1\)\n.*stw +r\d+,92\(\1\)\n.*stw +r\d+,112\(\1\)\n.*stw +r\d+,128\(\1\)']),
    ],
    'mouclass': [
        ('MOUSE_INPUT_DATA is 24 bytes (the input queue is stepped by 0x18)', [r'mulli +r\d+,r\d+,0x18']),
        ('IOCTL_INTERNAL_MOUSE_CONNECT is 0xF0203 with an 8-byte CONNECT_DATA', [r'lis +(r\d+),0xf\n.*ori +\1,\1,0x203\n.*li +r6,0x8']),
    ],
    'i8042prt': [
        ('the port driver reads IoControlCode at 0xC of the stack location it finds at irp + 0x60', [r'lwz +(r\d+),96\(r\d+\)\n(.*\n){0,2}.*lwz +r\d+,12\(\1\)']),
        ('CONNECT_DATA (8 bytes) arrives in Type3InputBuffer at 0x10 of the stack location: {ClassDeviceObject, ClassService}',
         [r'lwz +r\d+,8\(r\d+\)\n.*cmplwi +cr\d,r\d+,0x8', r'lwz +(r\d+),16\(r\d+\)\n(.*\n){0,2}.*lwz +r\d+,0\(\1\)\n(.*\n){0,3}.*lwz +r\d+,4\(\1\)']),
        ('KEYBOARD_ATTRIBUTES is 0x1C bytes; typematic parameters 6; indicator parameters 4; MOUSE_ATTRIBUTES 0xC',
         [r'lwz +r\d+,4\(r\d+\)\n.*cmplwi +cr\d,r\d+,0x1c', r'cmplwi +cr\d,r\d+,0x6', r'cmplwi +cr\d,r\d+,0x4', r'cmplwi +cr\d,r\d+,0xc']),
        ('KEYBOARD_INPUT_DATA: UnitId/MakeCode/Flags at 0/2/4, ExtraInformation at 8',
         [r'sth +r\d+,0\((r\d+)\)\n(.*\n){0,3}.*sth +r\d+,2\(\1\)\n(.*\n){0,3}.*sth +r\d+,4\(\1\)']),
        ('MOUSE_INPUT_DATA: Flags at 2, LastX/LastY at 0xC/0x10, ExtraInformation at 0x14',
         [r'stw +r\d+,12\((r\d+)\)\n(.*\n){0,2}.*stw +r\d+,16\(\1\)\n(.*\n){0,2}.*stw +r\d+,20\(\1\)\n(.*\n){0,3}.*sth +r\d+,2\(\1\)']),
        ('IOCTL codes: the keyboard family 0xB0004/8/20/40/80 and 0xB0203/403/803/1003',
         [r'lis +r\d+,0xb\n.*ori +r\d+,r\d+,0x' + v for v in ('203', '403', '803', '1003', '4$', '8$', '20$', '40$', '80$')]),
    ],
    'floppy': [
        ('DISK_GEOMETRY is 0x18 bytes with BytesPerSector 0x200', [r'cmplwi +cr\d,r\d+,0x18', r'li +r\d+,0x200']),
        ('the disk IOCTL family: IS_WRITABLE 0x70024, CHECK_VERIFY 0x74800, FORMAT_TRACKS 0x7C018',
         [r'lis +r\d+,0x7\n.*ori +r\d+,r\d+,0x' + v for v in ('24$', '4800', 'c018')]),
    ],
}


def driver_text(path):
    """All executable sections of a driver, disassembled with the TOC its entry descriptor names."""
    import struct
    d, opt, base, rd = pe_fn.load(path)
    entry_rva = struct.unpack_from('<I', d, opt + 16)[0]
    e1, toc = struct.unpack_from('<II', rd(base + entry_rva, 8), 0)
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    so = opt + optsz
    out = []
    for i in range(nsec):
        name = d[so + 40 * i:so + 40 * i + 8].rstrip(b'\0').decode()
        vs, va, rs, rp = struct.unpack_from('<IIII', d, so + 40 * i + 8)
        flags = struct.unpack_from('<I', d, so + 40 * i + 36)[0]
        if flags & 0x20 or name in ('.text', 'PAGE', 'INIT'):      # IMAGE_SCN_CNT_CODE
            out.append(section_text(path, f'{toc:x}', f'{base + va:x}', f'{base + va + vs:x}'))
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--kernel', required=True, help='the installed system\'s ntoskrnl.exe (or the CD\'s NTKRNLMP.EXE)')
    for drv in DRIVER_CHECKS:
        ap.add_argument(f'--{drv}', help=f'{drv}.sys, optional')
    a = ap.parse_args()
    bad = 0

    def report(ok, where, what):
        nonlocal bad
        print(f'  {"ok  " if ok else "MISS"}  {where:34} {what}')
        bad += 0 if ok else 1

    print(f'{a.kernel}:')
    cache = {}
    for export, n, what, pats in KERNEL_CHECKS:
        key = (export, n)
        if key not in cache:
            cache[key] = text_of(a.kernel, export, n)
        report(has(cache[key], *pats), export, what)
    for drv, checks in DRIVER_CHECKS.items():
        path = getattr(a, drv)
        if not path:
            continue
        print(f'{path}:')
        text = driver_text(path)
        for what, pats in checks:
            report(has(text, *pats), drv, what)
    print('\nall layouts confirmed' if not bad else f'\n{bad} pattern(s) not found: read the routine with pe-fn.py before trusting adbport.h on this build')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
