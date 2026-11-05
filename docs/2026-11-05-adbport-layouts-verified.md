<!-- SPDX-License-Identifier: GPL-2.0-only -->
<!-- Copyright (C) 2026 powermac-nt-hal contributors -->

# The NT structure layouts in `adbport.h`, read out of the shipped binaries

*5 November 2026. The driver's header declared a dozen NT 4.0 structures from the DDK's
documentation and pinned them with static asserts; one field was confirmed from a shipped
binary and the rest were not. This reads all of them out of the code of the kernel and of
Microsoft's own class and port drivers, and turns the reading into a check that runs against any
build.*

## 1. Where the evidence comes from

An installed NT 4.0 PowerPC disk carries the kernel and every driver Setup copied. The test
disk `winnt_4_0_ppc_ans_512mb.img` (gs-test-data) gave, through `tools/fatcat.py`:

| file | size | symbols |
|---|---|---|
| `\WINNT\SYSTEM32\NTOSKRNL.EXE` | 1,354,880 | stripped, but 981 exports with their descriptors |
| `\WINNT\SYSTEM32\DRIVERS\KBDCLASS.SYS`, `MOUCLASS.SYS` | 15,920 each | stripped; entry descriptor only |
| `\WINNT\SYSTEM32\DRIVERS\I8042PRT.SYS` | 38,928 | stripped |
| `\WINNT\SYSTEM32\DRIVERS\FLOPPY.SYS` | 32,336 | stripped |

All build 1381. Nothing was copied from them; each row below is a field offset observed in an
instruction, and the header's declarations were written from the DDK's documented layouts before
the reading began.

**Tools.** `tools/pe-fn.py <pe> <export>` resolves an export's `{entry, toc}` descriptor and
disassembles from the entry with that TOC (so TOC-relative loads annotate), which is what the
kernel work needed. `tools/pe-dis.py` learned to take a driver's TOC from its entry descriptor,
since `kbdclass.sys` and `mouclass.sys` have no `.data` section for its heuristic to scan, and
that the TOC anchor may sit past the image's last section (it is the TOC's start plus 0x8000).
`tools/check-nt-layouts.py` encodes every row below as an instruction pattern and reports which
are found.

## 2. The kernel

Unoptimised code, which helps: every field is a separate load or store with the offset in it.

### `DEVICE_OBJECT` is 0xB8 bytes — `IoCreateDevice`, 0x800b56c4–0x800b5824

```
800b56c4  addi  r8,r10,0xb8        ; r10 = object + extension size: DeviceObjectExtension follows the extension
800b56cc  stw   r8,176(r9)         ; DeviceObjectExtension at 0xB0
800b56d4  addi  r7,r7,0xb8         ; Size = sizeof(DEVICE_OBJECT) + extension
800b56ec  sth   r7,2(r9)           ; Size at 2
800b56f0  stw   r11,44(r9)         ; DeviceType at 0x2C
800b56f4  stw   r3,32(r9)          ; Characteristics at 0x20
800b56fc  sth   r4,0(r9)           ; Type 3 (IO_TYPE_DEVICE) at 0
800b5770  stw   r11,36(r8)         ; Vpb at 0x24 (file-system device types)
800b57c4  stw   r3,92(r10)         ; AlignmentRequirement at 0x5C (HalGetDmaAlignmentRequirement - 1)
800b57c8  sth   r9,172(r10)        ; SectorSize at 0xAC
800b57d4  stw   r8,28(r10)         ; Flags at 0x1C = DO_DEVICE_INITIALIZING (0x80), then |= 8 / 0x40
800b5818  addi  r9,r10,0xb8        ; DeviceExtension = object + 0xB8
800b581c  stw   r9,40(r10)         ;   at 0x28
800b5824  stb   r8,48(r10)         ; StackSize = 1 at 0x30
800b5860  addi  r3,r11,0x60        ; KeInitializeDeviceQueue(&DeviceQueue) at 0x60
800b5878  stw   r10,56(r11)        ; Queue.ListEntry at 0x34/0x38 for the exclusive types
800b58b4  stw   r10,8(r11)         ; DriverObject at 8
800b58bc  stw   r9,12(r11)         ; NextDevice at 0xC (the driver's previous first device)
```

### `IRP` is 0x70 bytes and a stack location 0x24 — `IoInitializeIrp`, 0x800384ac–0x800384f4

```
800384b4  stb   r30,34(r31)        ; StackCount at 0x22
800384b8  sth   r29,2(r31)         ; Size at 2
800384c4  stb   r10,35(r31)        ; CurrentLocation = StackCount + 1, at 0x23
800384cc  sth   r11,0(r31)         ; Type 6 (IO_TYPE_IRP) at 0
800384e4  stb   r9,38(r31)         ; ApcEnvironment at 0x26
800384e8  mulli r30,r30,0x24       ; StackCount * sizeof(IO_STACK_LOCATION)
800384f0  addi  r30,r30,0x70       ;   + sizeof(IRP)
800384f4  stw   r30,96(r31)        ; Tail.Overlay.CurrentStackLocation at 0x60
```

`IoAllocateIrp` (0x80036ae4) sizes the allocation the same way, `StackSize * 0x24 + 0x70`.

### The stack location's parameters — `IoBuildDeviceIoControlRequest`, 0x800373ec–0x8003762c

```
800373ec  lwz   r10,96(r10)        ; CurrentStackLocation
800373f0  addi  r9,r10,-0x24       ; the next location down: one stack location is 0x24
800373fc  stb   r11,-36(r10)       ; MajorFunction at 0: 0xF (internal) or 0xE
80037410  stw   r30,4(r9)          ; Parameters.DeviceIoControl.OutputBufferLength at 4
80037420  stw   r31,8(r9)          ;   InputBufferLength at 8
80037424  stw   r11,12(r9)         ;   IoControlCode at 0xC
8003761c  stw   r8,16(r9)          ;   Type3InputBuffer at 0x10 (METHOD_NEITHER)
80037460  stw   r10,8(r11)         ; Irp->Flags at 8
80037464  stw   r10,60(r11)        ; Irp->UserBuffer at 0x3C
800374a0  stw   r10,12(r11)        ; Irp->AssociatedIrp.SystemBuffer at 0xC
800374e8  stw   r9,8(r10)          ; Flags = IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER (0x30)
800375b0  stw   r3,4(r10)          ; Irp->MdlAddress at 4 (after IoAllocateMdl)
8003762c  stw   r10,40(r11)        ; Irp->UserIosb at 0x28
80037630  stw   r9,44(r11)         ; Irp->UserEvent at 0x2C
8003763c  stw   r8,80(r11)         ; Irp->Tail.Overlay.Thread at 0x50
80037674  stw   r9,16(r11)         ; Irp->ThreadListEntry at 0x10, onto the thread's IRP list
```

### Completion — `IofCompleteRequest`, 0x80037a84–0x80037d00

Reads `StackCount` (0x22) and `CurrentLocation` (0x23), advances `CurrentStackLocation` (0x60) by
0x24 per level, takes `IoStatus` at 0x18, and for each location reads `Control` at 3, the
`DeviceObject` at 0x14, `Context` at 0x20 and `CompletionRoutine` at 0x1C, calling the latter
through its descriptor (`lwz r0,0(r12); mtlr; lwz r2,4(r12)`).

### `MDL` — `MmMapLockedPages`, 0x8004df94–0x8004e128

```
8004df94  lwz   r11,16(r31)        ; StartVa at 0x10
8004df98  lwz   r4,24(r31)         ; ByteOffset at 0x18
8004dfac  lwz   r10,20(r31)        ; ByteCount at 0x14
8004e0e0  stw   r10,12(r31)        ; MappedSystemVa at 0xC
8004e0e4  ori   r9,r9,0x1          ; MDL_MAPPED_TO_SYSTEM_VA
8004e0f0  sth   r9,6(r31)          ; MdlFlags at 6
```

## 3. The class drivers

`kbdclass.sys` and `mouclass.sys` are the same source built twice; both lay their dispatch
table into the driver object at `DriverEntry` (kbdclass 0x123cc):

```
000123cc  stw   r11,56(r15)        ; MajorFunction[IRP_MJ_CREATE]           0x38
000123d0  stw   r10,48(r15)        ; DriverStartIo                          0x30
000123d4  stw   r11,64(r15)        ; MajorFunction[IRP_MJ_CLOSE]            0x40
000123d8  stw   r9,68(r15)         ; MajorFunction[IRP_MJ_READ]             0x44
000123dc  stw   r8,92(r15)         ; MajorFunction[IRP_MJ_FLUSH_BUFFERS]    0x5C
000123e0  stw   r7,112(r15)        ; MajorFunction[IRP_MJ_DEVICE_CONTROL]   0x70
000123e4  stw   r6,128(r15)        ; MajorFunction[IRP_MJ_CLEANUP]          0x80
```

`floppy.sys` does the same at 0x15838 (CREATE, READ, CLOSE, WRITE 0x48, DEVICE_CONTROL) and
stores `DriverUnload` at 0x34. So `MajorFunction` begins at 0x38, and `DriverStartIo`/`DriverUnload`
are at 0x30/0x34, as declared.

**The connect.** Both class drivers build the internal IOCTL the same way (kbdclass 0x12db4,
mouclass 0x12db4):

```
00012db4  lis   r3,0xb             ; 0xB0203 = IOCTL_INTERNAL_KEYBOARD_CONNECT (mouclass: lis 0xf)
00012db8  ori   r3,r3,0x203
00012dbc  li    r6,0x8             ; InputBufferLength = sizeof(CONNECT_DATA) = 8
00012dc0  li    r7,0x0             ; no output buffer
00012dc8  li    r9,0x1             ; InternalDeviceIoControl
                                    ; -> IoBuildDeviceIoControlRequest
```

**The input queue.** kbdclass divides and multiplies by 0xC (0x10e88, 0x1106c, 0x11230, 0x1147c);
mouclass by 0x18 at the same places. `KEYBOARD_INPUT_DATA` is 12 bytes and `MOUSE_INPUT_DATA` 24.

## 4. The port driver, `i8042prt.sys`

Microsoft's own keyboard and mouse port driver, which `adbport.sys` stands in for, is the best
witness for everything a port driver touches.

```
00010a7c  lwz   r10,96(r31)        ; the current stack location, from irp + 0x60
00010a84  lwz   r10,12(r10)        ; IoControlCode at 0xC
00010fd8  lwz   r11,8(r8)          ; InputBufferLength at 8 ...
00010fdc  cmplwi cr1,r11,0x8       ;   ... must be sizeof(CONNECT_DATA)
00010ff0  lwz   r8,16(r8)          ; Type3InputBuffer at 0x10
00010ff8  lwz   r11,0(r8)          ;   CONNECT_DATA.ClassDeviceObject
00011004  lwz   r8,4(r8)           ;   CONNECT_DATA.ClassService
00010dc4  lwz   r8,4(r8)           ; OutputBufferLength at 4 ...
00010dc8  cmplwi cr1,r8,0x1c       ;   ... sizeof(KEYBOARD_ATTRIBUTES) = 0x1C
00010e30  cmplwi cr1,r8,0x6        ; KEYBOARD_TYPEMATIC_PARAMETERS = 6
00010e8c  cmplwi cr1,r8,0x4        ; KEYBOARD_INDICATOR_PARAMETERS = 4
00011060  cmplwi cr1,r8,0xc        ; MOUSE_ATTRIBUTES = 0xC
00010edc  sth   r11,0(r10)         ; KEYBOARD_INPUT_DATA.UnitId
00010ee8  sth   r8,2(r10)          ;   MakeCode at 2
00010ef0  sth   r9,4(r10)          ;   Flags at 4
00011094  stw   r9,8(r10)          ;   ExtraInformation at 8
00011c18  stw   r29,12(r3)         ; MOUSE_INPUT_DATA.LastX at 0xC
00011c1c  stw   r28,16(r3)         ;   LastY at 0x10
00011c20  stw   r27,20(r3)         ;   ExtraInformation at 0x14
00011c28  sth   r30,2(r3)          ;   Flags at 2
000129e0  sth   r10,4(r9)          ;   ButtonFlags at 4
```

Its IOCTL constants, every one built as `lis rX,0xb; ori rX,rX,n`: n = 0x203, 0x403, 0x803,
0x1003 (connect, disconnect, enable, disable), 0x4 (set typematic), 0x8 (set indicators), 0x20
(query typematic), 0x40 (query indicators), 0x80 (query indicator translation). These are the
header's values to the digit. `DeviceObject->DeviceExtension` is read at 0x28 twenty-four times.

## 5. The floppy driver

`floppy.sys`'s `PAGE` section checks the geometry buffer against 0x18 (0x13a30) and fills
`BytesPerSector` with 0x200 (0x13f98); `DISK_GEOMETRY` is 24 bytes. Its disk IOCTLs built from
`lis 0x7`: 0x24 (`IS_WRITABLE`), 0x4800 (`CHECK_VERIFY`), 0xC018 (`FORMAT_TRACKS`) and 0xC02C
(`FORMAT_TRACKS_EX`); `GET_DRIVE_GEOMETRY` is the bare `lis 0x7`.

## 6. What this changes

Nothing in the binary: every declaration was already right, which the install and the logon had
shown. What changes is the status of each one, from *what the DDK says* to *what the shipped code
does, at this address*, and the existence of a check that can say the same for another build:

```bash
python3 tools/check-nt-layouts.py --kernel ntoskrnl.exe --kbdclass kbdclass.sys \
        --mouclass mouclass.sys --i8042prt i8042prt.sys --floppy floppy.sys
```

reports 21 patterns found against build 1381. A build that moves a field fails a named row.
