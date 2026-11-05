<!-- SPDX-License-Identifier: GPL-2.0-only -->
<!-- Copyright (C) 2026 powermac-nt-hal contributors -->

# Retiring the veneer: a plan for an ARC firmware of the project's own

*5 November 2026. A plan, not a result. With ledger row 6 gone
([`2026-11-05-the-vga-window.md`](2026-11-05-the-vga-window.md)) the only Microsoft bytes on the
floppy are `VENEER.EXE`, patched two ways (ledger rows 1–5 and 17). Those patches are what keeps a
built floppy from being shared, and the charter's section 7 named the exit in September: an ARC
firmware of the project's own. This note sizes that exit from the measurements already made and
from reading the one open-source ARC firmware for this chipset, and says how each step would be
tested in the emulator, most of it without the NT CD.*

## 1. What the veneer is, and what it does for us

Microsoft's `VENEER.EXE` (FirmWorks, 1996) is a 160 KB raw COFF image that Open Firmware loads
with `pe-loader` and starts with `go`. It is an **ARC firmware implemented on top of Open
Firmware's client interface**: every ARC call NT's loaders make is answered by calling Open
Firmware back. It has no drivers of its own; the SCSI controllers, the floppy, the console and the
display are Open Firmware's. What it adds is translation:

| ARC service | how the veneer provides it | where this project measured it |
|---|---|---|
| the configuration tree (`GetChild`/`GetPeer`/`GetParent`/`GetConfigurationData`) | walks the OF device tree and classifies nodes by `device_type` and `name` (`convert_name`, `convert_controller`) | row 17: `swim3` falls through to `OtherController`; `VrDebug 0x0008` traces the conversion |
| device and file I/O (`Open`/`Close`/`Read`/`Seek`/`Write`/`GetFileInformation`) | OF `open`, `read`, `seek` on the OF path it derives from the ARC path; a FAT and ISO reader of its own for files | rows 3, 4: the `partition(1)` and `:0` it appends; E3, E5, E14 |
| memory descriptors (`GetMemoryDescriptor`) | OF `/memory` `available` and its own claims | wall 1 (two failed claims, row 1), E24 (page-unaligned free ranges are fatal) |
| the environment (`GetEnvironmentVariable`/`Set`) | OF `/options` properties | E24, E26: `setenv` persists, so the environment is set per boot |
| time, restart | OF `get-msecs`; `reset-all` | wall 49's `HalReturnToFirmware` goes through Cuda instead |
| loading NT's loader (`Load`/`Invoke`/`Execute`) | its own PE loader: `SETUPLDR` off the CD, `OSLOADER.EXE` off the system partition | wall 47 (the boot-file path it hardcodes), the 98-page SETUPLDR and the `0x4A800` OSLOADER claims |
| the `SYSTEM PARAMETER BLOCK` and firmware vector | 63 entries, at the address NT's loaders expect | row 1's `nop`s are over its claim of that block |

The six patches, in those terms: rows 1 and 2 are bugs in the veneer's own OF glue (two claims
that fail on this firmware, an `OFClose` that reports the wrong return count); rows 3 and 4 are
its path-building assumptions about partitions and raw opens, right for a PReP box and wrong for
Apple's `disk-label`; row 5 is the SCSI model string Setup matches `symc810.sys` against; row 17
is the floppy's node name. **None of them is a limitation of Open Firmware.** A firmware written
for this machine's OF would not have any of them, which is the whole case for writing one.

## 2. Two shapes it could take

### A. Port maciNTosh-bandit's `arcbandit`

MCJack123's [`maciNTosh-bandit`](https://github.com/MCJack123/maciNTosh-bandit) (GPL-2.0, the
same licence as this project) is a complete ARC firmware for Bandit-class Power Macs: 21,600
lines in `arcbandit/source`, of which 4,600 are a console font. Read, not copied, for this plan:

- **It is a bare-metal firmware, not a shim.** A loader (`arcloader_bandit`, run by Open
  Firmware as an XCOFF at 0x50000) claims memory, sets up a framebuffer through the Control
  video chip, fills a `HW_DESCRIPTION` (memory size, Grand Central base, decrementer frequency,
  framebuffer geometry, boot device, ramdisk address) and jumps to `stage2.elf` at 0x80900000,
  which **never calls Open Firmware again**. From then on every device is its own driver:
  Cuda/PMU and ADB (`pxi.c`, `adb_bus.c`, `adb_kbd.c`), MESH SCSI (`scsi_mesh.c`), Mac I/O IDE
  (`ide.c`), SWIM3 floppy (`floppy_swim.c`), a 32bpp framebuffer console, FAT (`pff.c`), ISO 9660
  (`lib9660.c`), NTFS read-only, its own PE loader (`arcload.c`, 813 lines) and the ARC
  environment in NVRAM.
- **What the Network Server would need added:** a 53C825A SCSI driver for both controllers
  (maciNTosh has MESH only, and says so: *"there are no drivers for PCI SCSI controllers"*),
  the Cirrus 54M30 instead of Control for the framebuffer the loader sets up, and the ANS's
  interrupt and address map. The SCSI driver is the large item: the SCRIPTS engine the HAL
  currently never touches, because NT's `symc810.sys` drives it.
- **What it would buy beyond retiring the veneer:** a firmware that runs on a 7500/8500/9500 as
  well, since it does not need `pe-loader` or the NT ROM, and maciNTosh's existing NT install
  story on those machines.

### B. An ARC shim over Open Firmware, as the veneer is

Write what `VENEER.EXE` is, correctly for this firmware: a little-endian image Open Firmware
loads and starts, which answers the ARC vector by calling Open Firmware's client interface. No
hardware drivers at all: the 53C825As, SWIM3, the 54M30 text console and the serial port stay
Open Firmware's, exactly as they are under the veneer today.

Sized from the veneer's own symbol table (1,512 symbols, read in September) and from
`arcbandit`'s equivalents: the firmware vector and system parameter block (~200 lines), the OF
client-interface glue (`call-method`, `open`, `read`, `seek`, `claim`, `interpret`,
`getprop`/`nextprop`; `arcloader_bandit/source/ofapi.c` is 700 lines doing the same), the
device-tree-to-ARC conversion with this machine's names built in (~600), file I/O through OF with
a FAT reader for the system partition and ISO for the CD (~800, or OF's own `fat-files` and
`iso-9660-files` packages through `disk-label`, which the veneer does not use and which would
remove the readers), memory descriptors from `/memory` (~200), the environment on `/options`
(~150), a PE/COFF loader for `SETUPLDR` and `OSLOADER.EXE` (~600; `arcload.c` is the reference
for what NT's loader images need relocated and how), console through OF `stdout` (~100), and
restart through Cuda as the HAL already does (~50). **About 3,500 lines of C and a page of
assembly**, against a HAL of 2,800.

Why B first:

- It retires rows 1–5 and 17 **by construction**, with nothing new to drive. Every row is a
  fact about this firmware that the shim simply gets right.
- It is testable here, today, without the NT CD: the installed-system boot (`bootdisk.of`:
  firmware, `OSLOADER.EXE`, kernel, HAL, logon) exercises the whole ARC surface except reading an
  ISO. The emulator's `ans-nt-installed-boot` row, with the shim in place of `VENEERD.EXE`, is the
  acceptance test for most of the work.
- It also travels: it needs `xcoff-loader` (or `pe-loader`) and `little-endian?`, which every
  TNT ROM has. The one thing it does not do for a 7500/8500 is give them Open Firmware drivers
  they lack; where OF can see the disk and the display, the shim runs.
- A's drivers can follow later, inside the same ARC layer, if anyone wants to boot NT on a
  machine whose Open Firmware cannot see its disks.

## 3. The plan, with the test for each step

Every step has a measurable finish in the emulator; the first four need no CD.

| step | work | done when |
|---|---|---|
| **F1** | the image: clang/lld, little-endian, an XCOFF or PE wrapper `load fd:,\arcfw` accepts, `crt0` that saves OF's client-interface entry, a `printf` to OF `stdout` | `load fd:,\arcfw` + `go` prints a banner on ttya |
| **F2** | the firmware vector and system parameter block at the addresses NT expects; `GetMemoryDescriptor` from `/memory`; the environment on `/options`; `GetTime`/`GetRelativeTime` | a test program (our own, loaded as an ARC "OS") walks the vector and prints what the veneer printed with `VrDebug` |
| **F3** | the configuration tree from the OF device tree, with the ANS names right: `multi(0)scsi(0)disk(N)rdisk(0)partition(M)`, `cdrom`, `multi(0)disk(0)fdisk(0)` for `swim3`, the `NCR,825A` identifier `symc810.sys` matches, the 54M30 as the display controller | the tree matches the veneer's `dump_node` output (E7, 31 nodes) line for line, except where the veneer was wrong (row 17) |
| **F4** | file and device I/O over OF: `Open` of ARC paths to OF paths, raw sector reads, FAT on the system partition; the PE loader; `Invoke`/`Execute` | **`ans-nt-installed-boot` passes with the shim in place of `VENEERD.EXE`**: `OSLOADER.EXE` off partition 1, the kernel, this HAL, the logon screen, the desktop. `bootdisk.of` shrinks to "load the shim, go" |
| **F5** | ISO 9660 for the CD, `\PPC\SETUPLDR`, the OEM-disk hand-off (`BOOT.OF`'s RAM image stays; the shim can place it itself) | **`ans-nt-install` passes with the shim in place of `VENEER.EXE`**; needs the CD, one run |
| **F6** | remove `mkveneer.py`, the veneer from `mkfloppy.py`, rows 1–5 and 17 from the ledger; the floppy is six files, all this project's, **and can be shipped** | `make floppy` needs no ISO at all |
| F7 | later, if wanted: shape A's drivers for machines whose OF cannot reach the disk | a 7500 or 8500 |

What is already in hand for F2–F4: the veneer's `VrDebug` traces of every ARC call SETUPLDR and
OSLOADER make (the 2026-09-07 and 2026-09-15 notes), the ARC tree dump, the measured claim
constraints (E24), `arcload.c` as a GPL reference for the PE loader's relocation rules, and
`tools/coffdis.py` for reading the veneer wherever a behaviour needs confirming.

## 4. Provenance and licence

Shape B is original work over documented interfaces: the ARC specification (the firmware vector,
the configuration tree, the memory descriptors, the environment) and the IEEE 1275 client
interface. Where `arcbandit` is read for shape, the row goes in `PROVENANCE.md` as "rewritten from
reading", as the HAL's `halartx` rows do; both are GPL-2.0, so adapting would also be permitted,
and this project's habit is to read. Nothing of Microsoft's is read for this beyond what the
veneer's own symbol table and traces already gave in September; nothing is copied from it.

## 5. What this is not

It is not the next thing to do before the install path of the VGA-window change has been run
once with the CD, and it is not small: F1–F4 is a few weeks of work for someone with the
emulator and the notes. It is, after the display pair, the last Microsoft file on the floppy,
and the only thing between this project and a downloadable image.
