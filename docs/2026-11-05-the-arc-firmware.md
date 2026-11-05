<!-- SPDX-License-Identifier: GPL-2.0-only -->
<!-- Copyright (C) 2026 powermac-nt-hal contributors -->

# An ARC firmware of the project's own: `arcfw.exe` in place of Microsoft's veneer

*5 November 2026. The result section says how far it got. The plan is [`2026-11-05-retiring-the-veneer.md`](2026-11-05-retiring-the-veneer.md),
shape B: an ARC firmware implemented over Open Firmware's client interface, the shape
`VENEER.EXE` itself has, with no hardware drivers of its own. This note records what had to be
measured to write it, since nothing about the veneer's contract with NT's loaders is documented
anywhere that could be read.*

## 1. How the veneer is entered, and leaves

Read from `VENEER.EXE`'s own code with `tools/coffdis.py` (it ships with its symbol table):

- Open Firmware's `pe-loader` **copies the file flat**: byte `SizeOfHeaders + n` of the file
  lands at `ImageBase + n`, whatever the section table says, and the first instruction executed
  is the one at `ImageBase`, whatever `AddressOfEntryPoint` says. The veneer is built that way
  (every section's `PointerToRawData` is its RVA + 0x200, so file and memory agree). A first
  `arcfw.exe` with its code at RVA 0x1000 took a DSI at `0x50004`, executing its own COFF
  header; a second, with the sections packed by file alignment, had its `.rdata` strings land in
  `.text`'s padding and its `.data` a page off, and Open Firmware answered every client-interface
  call with -1 because the service name it was handed read as `""` (a side-by-side probe of the
  veneer's `OFFinddevice` and ours at the `bctr`). `elf2pe.py --raw-coff` now pads each section
  to its RVA stride, so the file is an image of memory.
- pe-loader claims nothing beyond the file. The veneer's `.bss` has 0x800 bytes of file behind
  it; `arcfw.exe`'s data and bss are file-backed too.
- At entry (`start`, 0x50000): r5 is the client-interface handler, r1 a stack of Open
  Firmware's, the MMU on with Open Firmware's translations (`SRR1 0x3071`: IR, DR, FP, ME, IP,
  LE). The veneer stores r5 and calls `main`; `call_firmware` is `mtctr r5; bctr` with the
  argument array in r3.
- `main`'s order: `VrInitSystemBlock` (the block at 0x4000, 0xD0 bytes, zeroed),
  `read_ARC_env_vars`, `parse_args`, `VrKseg0`, `walk_obp` (the tree), `move_multi_to_root`,
  `VrInitSystem` (the vector), `find_boot_dev`, `create_argv`, `VrOpen` + `load_file` of the
  boot file, `VrFlushAllCaches`, `VrCreateMemoryDescriptors`, then
  `lwz r0,0(entry); mtlr r0; lwz r2,4(entry); blrl` with argc, argv, envp in r3..r5: the entry
  is the loader's `{entry, toc}` descriptor, and the loader runs on the veneer's stack.
- `VrKseg0`: `call-method "map"` on `/chosen`'s mmu with `(phys 0, virt 0x80000000, size
  0x800000, mode -2)`: **8 MB of KSEG0**. `claimreal(addr, size)` is `claim` on the memory node
  of `addr & 0x7FFFFFFF` then `map` of it at `addr` with mode -1. `load_file` reads the COFF
  header, checks machine 0x1F0 and magic 0x10B, `claimreal`s `ImageBase` for `SizeOfImage`,
  zeroes it, and reads each section to `ImageBase + VirtualAddress`. No relocation.
- The call-method argument array lists the Forth stack picture **top first**: `map`'s registers
  are r8 = mode, r9 = size, r10 = virt, then phys; `set-address` is r8 = target, r9 = lun.

## 2. What NT's loader asked for

The veneer's own `VrDebug` word (at `0x60C08` in this build, poked to 0x7FFF at the breakpoint
on its first instruction) printed every ARC call OSLOADER made on the way to the kernel:

| calls | count | what |
|---|---|---|
| `GetEnvironmentVariable` | 48 | OsLoader, SystemPartition, OSLoadFilename, OSLoadPartition, OSLoadOptions, LoadIdentifier, AutoLoad, FWSearchPath, LastKnownGood, FWTEST (the veneer's own), then PAGEKERNEL, HPTSIZE, LastKnownGood, ICACHEMODE, DCACHEMODE (the loader's) |
| `GetChild`/`GetPeer`/`GetConfigurationData` | 82/62/24 | the whole tree, every component's data copied into the loader block |
| `GetMemoryDescriptor` | 48 | the list walked from NULL to NULL, several times |
| `Open` | 21 | `multi(0)serial(0)line(0)` twice (console out, mode 1; console in, mode 0: file ids 2 and 3), then `multi(0)scsi(1)disk(0)rdisk(0)partition(1)` and `partition(2)` repeatedly |
| `Read`/`Seek` | 938 each | 512-byte sector reads and 1536-byte runs at byte offsets, the loader's own FAT code |
| `Write` | 34 | the console: `OS Loader V4.00`, the countdown's dots |
| never | | `GetTime`, `GetRelativeTime`, `GetDisplayStatus`, `GetFileInformation`, `Mount`, `Load`, `Execute` (not traced by VrDebug, or not called) |

`argv` as the veneer built it: the OSLOADER path; `OsLoader=`, `SystemPartition=`,
`OSLoadFilename=`, `OSLoadPartition=`, `OSLoadOptions=`, `LoadIdentifier=` with the values of
those variables; `ConsoleIn=multi(0)serial(0)line(0)`, `ConsoleOut=multi(0)serial(0)line(0)`.

## 3. The tables, byte for byte

Read out of memory with the emulator once OSLOADER had started (the shell's peek shows the
word at A at A ^ 4 in little-endian mode, each word byte-reversed; the decoder undoes both).

**The system parameter block** at 0x4000: signature `ARCS` (0x53435241), length 0x3C, version
2.0, RestartBlock 0x800040D0, DebugBlock 0, exception vectors 0, FirmwareVectorLength 0x94 (37
entries), FirmwareVector 0x8000403C, no vendor vector, no adapters. Every vector slot holds the
address of an **{entry, toc} descriptor** in the veneer's `.rdata`, and the three reserved and
`TestUnicodeCharacter` slots point at one stub. The restart block at 0x40D0: `RSTB`, length
0x13C, version 1.2, next 0, address 0, master 0, processor 0, BootStatus 0x1C0, SaveAreaLength
0x114, and a checksum.

**The configuration tree** (class/type, key, flags, identifier, configuration data):

```
System/ArcSystem key 0 "device-tree"          106 bytes: list v1.2 n=1, DeviceSpecific 0x52 bytes:
                                               "Firmware\0OpenFirmware2.26\0Veneer\0FirmWorks,ENG,3.0,...\0"
  Processor/CentralProcessor key 0 "PowerPC(604)"
    Cache/PrimaryIcache key 0x01050002           0x01 | log2(32-byte line) << 16 | log2(4 pages)
    Cache/PrimaryDcache key 0x01050002
  Memory/SystemMemory key 0 "memory"            24 bytes: Memory 0 + 0x04000000
  Adapter/MultiFunctionAdapter key 0 "PCI"      24 bytes: Port (IO) 0xF2000000 + 0x02000000
    Adapter/ScsiAdapter key 0 "NCRC810"          40 bytes: Port 0 + 0x100, Interrupt level 1 vector 0 (unusable; the HAL repairs it)
      Controller/CdromController key 0 flags 0x26 "SONY    CD-ROM CDU-8002 1.8g"
        Peripheral/FloppyDiskPeripheral key 0 flags 0x26
    Adapter/ScsiAdapter key 1 "NCRC810"          40 bytes, the same
      Controller/DiskController key 0 flags 0x60 "QUANTUM LPS540S         590A"
        Peripheral/DiskPeripheral key 0 flags 0x60
    Adapter/ScsiAdapter key 0 "UNKNOWN SCSI"     24 bytes: a null descriptor (the 53c94)
    Controller/NetworkController key 0 flags 0x60 "UNKNOWN NETWORK"   24 bytes: null descriptor
      Peripheral/NetworkPeripheral key 0
    Controller/SerialController key 0 flags 0x78 "ch-a"   48 bytes: null descriptor + DeviceSpecific 8: 01 00 02 00 00 20 1c 00
      Peripheral/LinePeripheral key 0
    Controller/SerialController key 1 flags 0x78 "ch-b"   (the same, key 1)
      Peripheral/LinePeripheral key 1
    Controller/OtherController key 0 flags 0x60 "swim3"   24 bytes: null (DiskController/FloppyDiskPeripheral with ledger row 17)
      Peripheral/OtherPeripheral key 0
    Controller/KeyboardController key 0 flags 0x28 "adb"
      Peripheral/KeyboardPeripheral key 0 "PCAT_ENHANCED"
    Controller/PointerController key 0 flags 0x20 "adb"
      Peripheral/PointerPeripheral key 0 "PS2 MOUSE"
    Controller/DisplayController key 0 flags 0x50 "STB,HORIZON+"   24 bytes: Memory 0 + 0x01000000
      Peripheral/MonitorPeripheral key 0 "1024x768"
  Cache/SecondaryCache key 0x01030007
  Adapter/MultiFunctionAdapter key 1 "PCI"      24 bytes: Port 0xF4000000 + 0x02000000
  Adapter/MultiFunctionAdapter key 2 "pci106b-1"
  Adapter/MultiFunctionAdapter key 3 "pci106b-1"
```

Every component is version 2.0 with AffinityMask 0xFFFFFFFF. Grand Central's devices are
children of the first PCI adapter; the `gc` node itself is dropped. The SCSI targets came from a
probe through Open Firmware: `open` the controller, then for each target but the initiator's
`call-method "set-address" (target, lun 0)` and `"short-data-command" (6, cdb, 0xFF)` with an
INQUIRY, which returns a status and the package's buffer; byte 0 is the peripheral type and
bytes 8..35 the identifier. The display's identifier is the Open Firmware `model`.

**The memory descriptors** (type, first page, pages): FirmwarePermanent 0..0xA; Free 0xA..0x40;
FirmwareTemporary 0x40..0x78 (the veneer); Free 0x78..0x600; LoadedProgram 0x600..0x64B
(OSLOADER); Free 0x64B..0x800; FirmwareTemporary 0x800..0x3D00 (**everything above 8 MB**), and
0x3D00..0x4000 in five pieces (the boot script's claims and Open Firmware at real-base). The
free ranges are Open Firmware's `available` property at the time; what is not available is
FirmwareTemporary, which the kernel reclaims. NT's loader therefore places everything below 8 MB,
inside the KSEG0 map.

## 4. The Open Firmware tree it is made from

`dev / ls` on the running machine: `/PowerPC,604` (cpu; `cpu-version 0x00040103`, caches 16 KB
with 32-byte lines, `l2-cache` 512 KB unified), `/memory` (`reg 0 0x04000000`, `available 0..0x400000,
0x500000..`), `/bandit@F2000000` (pci; `53c825@11` and `@12` with `model NCR,825A`,
`AAPL,interrupts` 0x16 and 0x1A, `assigned-addresses` giving the I/O BAR at 0x400 / 0x800 and the
register BAR at 0xF3100000 / 0xF3103000; `gc@10` with `53c94`, `mace`, `escc/ch-a`, `escc/ch-b`,
`swim3`, `via-cuda/adb/keyboard`, `via-cuda/adb/mouse`; `pci106b,1@B`; `54m30@F` with `model
STB,HORIZON+`), `/bandit@F4000000` (with its `pci106b,1`), `/hammerhead`. The SCSI package's
methods include `set-address`, `short-data-command`, `inquiry`, `probe-target`, `show-children`.

## 5. Result

`arcfw.exe` boots the installed Windows NT 4.0 to its logon screen on the emulator, in place of
the veneer: granny-smith's `ans-nt-installed-boot` row, pointed at a floppy built with
`--arc-firmware build/arcfw.exe`, passes rungs B1 to B4 (firmware prompt, little-endian reboot,
NT on the HAL with the ADB classes connected and the system in D:\WINNT, the logon screen).
Rung B5 (Ctrl+Alt+Del, the Logon Information dialog, Return, the desktop): the Logon
Information dialog matches, Administrator logs on, and the desktop comes up with its *Welcome
to Windows NT* dialog, pixel for pixel the row's golden but for the taskbar clock (see below).
With the probe and the image trimmed, the kernel's banner comes 7.1 billion instructions after
power-on through arcfw against 9.8 billion through the veneer: this firmware is the faster of
the two.

What it took, after the image loaded and ran -- each one measured, none guessed:

1. **The flat file layout** above (section 1): the first symptom was every Open Firmware call
   returning -1.
2. **Integer properties are big-endian in the device tree** whatever mode the processor runs
   in, and this firmware runs little-endian. `/chosen`'s `stdout` read as `0x00658dff`, which is
   `0xff8d6500` -- an address inside Open Firmware -- byte-reversed, and `write` on it answered
   -1. Every cell `getprop` returns is swapped (`of_get_ints`); strings are left alone.
3. **The last-page sliver.** `bootdisk.of` claimed the rest of OSLOADER's last page before `go`,
   a workaround for the veneer's page-unaligned claim (E24). `arcfw` claims the loader's pages
   whole, so the sliver collided with its claim: `cannot claim 4a800 bytes at 80600000`. The
   script no longer claims it when the image is `ARCFW.EXE`.
4. **Free memory stops at KSEG0's 8 MB.** The loader takes its heap and stack from a free
   descriptor through KSEG0 addresses, and KSEG0 is mapped for 8 MB: with free pages above it
   on the list, OSLOADER died in `RtlZeroMemory` (DSI at `0x80611B58`, named from the symbol
   table OSLOADER ships). Everything above 8 MB is FirmwareTemporary, as the veneer had it; the
   kernel reports 64 MB all the same, and reclaims it.
5. **`GetEnvironmentVariable` returns the value or NULL.** The vector's entry is the ARC one
   (`PCHAR (PCHAR name)`), not the kernel's three-argument status-returning form. The first
   arcfw returned ENOENT; OSLOADER took 2 for a string, read `HPTSIZE` as set, took the
   small-machine path in `BlPpcMemoryInitialize` (a breakpoint at `0x806042A8` showed r31 =
   0x10000 pages against the veneer's 0x4000) and the kernel died with bugcheck 0x50 at
   `0x807F80C8` as user mode started -- a blue screen that sat still after `adbport: keyboard
   class connected`, caught with a breakpoint on `KeBugCheckEx`.

And one measurement that is not a defect: the row's desktop golden differs from what arcfw
boots to only in the taskbar clock. NT's clock read six emulated minutes later because the
firmware phase ran 2.7 billion more instructions; from the kernel's banner to the logon screen
the two boots take the same time. Where the firmware phase went, from the heartbeat counts
around the console lines: reading a 425-block image off the floppy instead of 316 cost 0.2
billion; **the SCSI probe cost 2.3 billion per controller**, one selection timeout of 0.38
billion for each of the thirteen absent targets. The veneer's tree never had anything but
target 0, and now neither does arcfw's unless the boot variables name another `disk(N)` or
`ARCFW-SCSI-TARGETS` on `/options` asks (`0,3,5`, or `all`). The image also carried its 64 KB
stack and 96 KB heap as file bytes (pe-loader copies the file flat), so both now come from Open
Firmware -- its own stack, as the veneer uses it, and a `claim`ed heap -- and `ARCFW.EXE` is 53
KB, 105 blocks: a third of the veneer's read. The golden still cannot match pixel for pixel,
since the clock cannot be made to agree; a row that boots through arcfw needs its own floppy and
goldens in gs-test-data.

## 6. The install boot: SETUPLDR off the CD

With a Windows NT 4.0 Workstation for PowerPC CD at hand (the stock October 1996 pressing:
`CDROM_W.40`, a `VENEER.EXE` on which `mkveneer.py` verifies every patch site), `BOOT.OF` was
pointed at `ARCFW.EXE` as well, with the ARC environment for the install set on `/options` for
the boot (`SYSTEMPARTITION` the CD, `OSLOADER` `\\PPC\\SETUPLDR` on it) and the OEM-disk staging
unchanged. What it took:

1. **Open Firmware's `iso-9660-files` package opens a file and then refuses it.** `Open` of
   `multi(0)scsi(0)cdrom(0)fdisk(0)\\PPC\\SETUPLDR` as `sd@0,0:,\\PPC\\SETUPLDR` gave an instance;
   `seek` on it answered -1, `read` answered -1, and `call-method "load"` threw inside the
   firmware (it printed its banner and `can't LOAD from this device`). The FAT package behaves:
   OSLOADER off the system partition is read with seek and read.
2. **So the firmware finds CD files itself**: the device opened raw (which reads and seeks at
   byte offsets, as SETUPLDR itself uses it, E3), the primary volume descriptor at sector 16, a
   walk of the directory records for each path component (`io_open_iso_file`), and the image
   read at `extent * 2048 + offset`.
3. **The alignment wall, once more.** The four byte loads that assemble a little-endian field
   of a directory record were folded by clang into one `lwz` at a misaligned address, which is
   an alignment exception on this 604 (`DEFAULT CATCH` code `FFF00600` at `0x54FFC`); a volatile
   pointer keeps them bytes.

With those, `arcfw` loads SETUPLDR (`0x80600000..0x80647200`, six sections) and enters it, and
SETUPLDR opens the CD raw, draws *Windows NT Setup* and asks for the computer type. Keys reach
it through the firmware console (`GetReadStatus`/`Read` on `multi(0)serial(0)line(0)`), it
reads the support floppy through `multi(0)disk(0)fdisk(0)` (the `swim3` node, raw), takes the
ADB driver off it, and starts the kernel: the HAL, the ADB driver with the OEM disk at
`0x3B98000`, the keyboard class, the drive letters, Setup's Welcome. Two things learned on the
way, neither a defect of the firmware: NT's loaders read an arrow key on an ARC console as the
single CSI byte `0x9B` followed by the letter, not as `ESC [ A` (the row's video-adapter choice
is `\x9bA` twice, from *Other* to the CD's *Motorola Power Stack (cirrus 54xx)*); and the
emulator keeps 64 KB of transmitted serial between drains of `scc.a.sent()` and drops the rest,
so a probe that drains rarely sees the console "stop" mid-line while the machine is fine. Granny
Smith's `ans-nt-install` row, pointed at the floppy `make floppy` now builds, then runs text-mode
Setup and GUI Setup to the Registration page (rung N10, the last one without a Product ID). The
veneer is retired from both boots, every one of the ledger's veneer rows (1 to 5) with it, and
`make floppy` takes nothing off any CD: the floppy is this project's, GPL-2.0-only, and may be
redistributed. `release.yml` publishes it.

Tools: `elf2pe.py --raw-coff` for the image; `coffsyms.py`/`coffdis.py` for OSLOADER, a raw
COFF with 599 function symbols (`..BlPpcMemoryInitialize`, `..AllocateKseg0Blocks`,
`..RtlZeroMemory` and the rest name every fault at once); granny-smith's headless shell for the
breakpoints, which must be **logical** addresses for code under KSEG0 -- a physical breakpoint
on OSLOADER or the kernel never fires.
