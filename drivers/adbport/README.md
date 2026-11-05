<!-- SPDX-License-Identifier: GPL-2.0-only -->
<!-- Copyright (C) 2026 powermac-nt-hal contributors -->

# adbport.sys

This project's own Windows NT 4.0 PowerPC driver for the Apple Network Server's Apple Desktop
Bus keyboard and mouse — and for the OEM disk image the boot floppy leaves in RAM, which it
serves to Setup as `\Device\Floppy0`.

**Status: loaded and working — text-mode Setup completes with it (16 September 2026).** SETUPLDR
loads it under `[SCSI]`, `DriverEntry` brings up all three devices (`adbport: up; keyboard,
pointer, OEM disk as \Device\Floppy0`), `kbdclass` connects (`keyboard class connected`), every
Setup screen from Welcome to *"This portion of Setup has completed successfully"* was driven from
the ADB keyboard, and Setup's OEM-file copy read the RAM floppy through `\Device\Floppy0`. The
mouse path is exercised by the floppy-booted installed system: `mouclass` connects and, when GUI
Setup opens the pointer class, enables the port — which found the one fault so far *in* this
driver: it did not announce its ports under `HARDWARE\DEVICEMAP\KeyboardPort` / `PointerPort`,
and `mouclass` enabled a port that was not ours (STOP 0x35, E27). `DriverEntry` writes both
values now, as every NT 4 port driver does. See the boot-floppy note, §4.7 and E21–E27.

## Why it exists

A stock NT 4.0 CD installs on this machine with everything of ours on a floppy (the plan above),
and text-mode Setup runs through every screen — until it copies the OEM files onto the hard disk.
That copy happens under NT, through an NT floppy device, and NT has no driver for the SWIM3
behind the real drive. So `\BOOT.OF` reads the whole floppy into RAM before `go`, the HAL checks
and fences that region (`src/oemdisk.c`), and this driver serves it as drive A:. The keyboard and
mouse are in the same binary because Setup loads OEM drivers through one prompt that accepts any
kernel driver — the SCSI one — and each extra driver is another trip through it. It replaces
maciNTosh's `usbadb.sys`, which this project may run but may not redistribute (ledger row 9).

## What it does

| device | how |
|---|---|
| `\Device\KeyboardPort0` | the HAL hands us every ADB packet from its DPC through `HalPxiAdbSetCallback`; ADB address 2, Talk Register 0, becomes set-1 scancodes for `kbdclass` (`kbd.c`) |
| `\Device\PointerPort0` | ADB address 3 becomes `MOUSE_INPUT_DATA` for `mouclass` (`mouse.c`) |
| `\Device\Floppy0` | `HalAnsOemDiskQuery` says where the image is; `MmMapIoSpace` maps it; reads, writes and the disk IOCTLs `setupdd` and `fastfat` send a floppy are answered from RAM (`ramdisk.c`) |

The hand-off is `include/oemdisk.h`, shared by `tools/mkbootfloppy.py` (writes the header),
the HAL (validates it, marks the pages firmware-permanent) and this driver.

## Building

`make` at the top of the repository builds `build/adbport.sys` next to `build/hal.dll`, with the
same clang/lld/`elf2pe.py` toolchain. `adbport.imports` names two DLLs; `tools/mkstubs.py` and
`elf2pe.py` learnt import groups for it.

## What is verified, and what is not

**The NT 4.0 structure layouts in `adbport.h` are verified**, as of 5 November 2026, against the
code of the shipped kernel and of Microsoft's own class and port drivers taken off an installed
system: `IoCreateDevice` for `DEVICE_OBJECT`, `IoInitializeIrp`, `IoBuildDeviceIoControlRequest`
and `IofCompleteRequest` for the `IRP` and `IO_STACK_LOCATION`, `MmMapLockedPages` for the `MDL`,
`kbdclass.sys` and `floppy.sys` for `DRIVER_OBJECT`'s dispatch table, `kbdclass`/`mouclass` for
the connect IOCTLs and the input-record strides, `i8042prt.sys` for the input records, the
attribute sizes and every keyboard IOCTL code, and `floppy.sys` for `DISK_GEOMETRY`. Each
declaration in the header carries the routine and image address it was read from;
[`docs/2026-11-05-adbport-layouts-verified.md`](../../docs/2026-11-05-adbport-layouts-verified.md)
is the account, and [`tools/check-nt-layouts.py`](../../tools/check-nt-layouts.py) re-reads the
same instruction patterns from any build's binaries:

```bash
python3 tools/check-nt-layouts.py --kernel ntoskrnl.exe --kbdclass kbdclass.sys \
        --mouclass mouclass.sys --i8042prt i8042prt.sys --floppy floppy.sys
```

Run it before trusting this driver on a kernel other than build 1381 (`fatcat.py` takes the files
off an installed disk image).

Still unverified, in order of likelihood of mattering:

1. Which DLL exports what on another build. `KeAcquireSpinLock` from the kernel and `KeRaiseIrql`
   from the HAL is how build 1381 has it, and the driver loads; a wrong guess on another build is
   an unresolved import at load, not a silent fault.
2. Whether `kbdclass` cares about the attributes we report beyond their size, and whether Setup's
   key handling needs the E0 flags exactly as set. Every screen of Setup and the logon chord have
   been driven from this driver, so whatever it needs, it gets.
3. The OEM disk's physical address (`0x03B97000`) surviving SETUPLDR's own allocations on a
   machine with a different memory size. The HAL validates the header and the FAT boot sector, so
   a collision is a trace line, not garbage.

Delivered under `[SCSI]` by `txtsetup.oem`; at the mass-storage screen, `S`, `Other`, Enter.
