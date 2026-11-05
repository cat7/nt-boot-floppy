<!-- SPDX-License-Identifier: GPL-2.0-only -->
<!-- Copyright (C) 2026 powermac-nt-hal contributors -->

# An ARC firmware of the project's own: `arcfw.exe` in place of Microsoft's veneer

*5 November 2026. Work in progress while this note is being written; the result section says
how far it got. The plan is [`2026-11-05-retiring-the-veneer.md`](2026-11-05-retiring-the-veneer.md),
shape B: an ARC firmware implemented over Open Firmware's client interface, the shape
`VENEER.EXE` itself has, with no hardware drivers of its own. This note records what had to be
measured to write it, since nothing about the veneer's contract with NT's loaders is documented
anywhere that could be read.*

## 1. How the veneer is entered, and leaves

Read from `VENEER.EXE`'s own code with `tools/coffdis.py` (it ships with its symbol table):

- Open Firmware's `pe-loader` lays the raw COFF's sections at `ImageBase + RVA` **and maps no
  headers**: the first instruction executed is the one at `ImageBase`, whatever
  `AddressOfEntryPoint` says. The veneer's `.text` is RVA 0; so is `arcfw.exe`'s. A first build
  with its code at RVA 0x1000 took a DSI at `0x50004`, executing its own COFF header.
- pe-loader copies what is in the file and claims nothing beyond it. The veneer's `.bss` has
  0x800 bytes of file behind it; `arcfw.exe`'s data and bss are file-backed too
  (`elf2pe.py --raw-coff`).
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

*(being measured)*
