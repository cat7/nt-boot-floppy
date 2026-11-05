/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * Original work of this project.  The NT structure layouts below are DDK-documented interface
 * facts, re-declared here in our own words and pinned with static asserts; the IOCTL codes are
 * computed from the documented CTL_CODE formula.  Nothing derives from leaked Windows NT source
 * and nothing is copied from another driver.  See PROVENANCE.md.
 *
 * adbport.h — adbport.sys: the Apple Desktop Bus keyboard and mouse as NT port devices, and the
 * OEM disk image the boot floppy left in RAM as \Device\Floppy0.
 *
 * Why one driver does three things.  Text-mode Setup loads OEM drivers through exactly one prompt
 * that takes arbitrary kernel drivers, the SCSI one, and each pick costs the user a trip through
 * "S, Other, Enter".  Keyboard, mouse and the disk Setup copies the OEM files from are what an
 * ANS install needs and a stock CD cannot supply, so they arrive together.  maciNTosh's driver
 * has the same shape for the same reason; ours is written to our own contract (oemdisk.h) and
 * our own HAL.
 *
 * STATUS: loaded and working since 16 September 2026 (text-mode and GUI Setup, the installed
 * system's logon).  Every NT layout below is marked VERIFIED with the shipped routine it was read
 * from -- the kernel's IoCreateDevice, IoInitializeIrp, IoBuildDeviceIoControlRequest,
 * IofCompleteRequest and MmMapLockedPages, and kbdclass.sys, mouclass.sys, i8042prt.sys and
 * floppy.sys -- all NT 4.0 build 1381 for PowerPC.  tools/check-nt-layouts.py re-reads those
 * patterns from another build's binaries; docs/2026-11-05-adbport-layouts-verified.md has the
 * evidence. */
#pragma once
#include "nt.h"
#include "oemdisk.h"

/* ---- the kernel objects a driver touches ---------------------------------------------------- */

typedef ULONG KSPIN_LOCK, *PKSPIN_LOCK;
typedef struct _KDEVICE_QUEUE { CSHORT Type, Size; LIST_ENTRY DeviceListHead; KSPIN_LOCK Lock; BOOLEAN Busy; UCHAR Pad[3]; } KDEVICE_QUEUE;
_Static_assert(sizeof(KDEVICE_QUEUE) == 20, "KDEVICE_QUEUE is 20 bytes on NT 4.0");

/* NT 4.0's DEVICE_OBJECT, 0xB8 bytes.  Only the fields this driver reads or writes are named;
 * the rest keep their place.  VERIFIED against the shipped kernel's IoCreateDevice (ntoskrnl.exe
 * build 1381, image VA 0x800b56c4..0x800b5824): Type 3 at 0, Size at 2, Flags 0x1C,
 * Characteristics 0x20, DeviceExtension 0x28 = object + 0xB8, DeviceType 0x2C, StackSize 0x30,
 * Queue 0x34, AlignmentRequirement 0x5C, DeviceQueue 0x60, SectorSize 0xAC,
 * DeviceObjectExtension 0xB0.  docs/2026-11-05-adbport-layouts-verified.md; re-check another
 * build with tools/check-nt-layouts.py. */
typedef struct _DEVICE_OBJECT {
    CSHORT Type, Size;                  /* 0x00 */
    LONG ReferenceCount;                /* 0x04 */
    PDRIVER_OBJECT DriverObject;        /* 0x08 */
    PDEVICE_OBJECT NextDevice;          /* 0x0C */
    PDEVICE_OBJECT AttachedDevice;      /* 0x10 */
    PIRP CurrentIrp;                    /* 0x14 */
    PVOID Timer;                        /* 0x18 */
    ULONG Flags;                        /* 0x1C */
    ULONG Characteristics;              /* 0x20 */
    PVOID Vpb;                          /* 0x24 */
    PVOID DeviceExtension;              /* 0x28 */
    ULONG DeviceType;                   /* 0x2C */
    CHAR StackSize; UCHAR Pad0[3];      /* 0x30 */
    UCHAR Queue[40];                    /* 0x34  LIST_ENTRY | WAIT_CONTEXT_BLOCK */
    ULONG AlignmentRequirement;         /* 0x5C */
    KDEVICE_QUEUE DeviceQueue;          /* 0x60 */
    ULONG Dpc[8];                       /* 0x74  a KDPC, 32 bytes here; nt.h's KDPC is padded to 64 for the HAL's own use */
    ULONG ActiveThreadCount;            /* 0x94 */
    PVOID SecurityDescriptor;           /* 0x98 */
    KEVENT DeviceLock;                  /* 0x9C */
    USHORT SectorSize, Spare1;          /* 0xAC */
    PVOID DeviceObjectExtension;        /* 0xB0 */
    PVOID Reserved;                     /* 0xB4 */
} DEVICE_OBJECT;
_Static_assert(sizeof(DEVICE_OBJECT) == 0xB8, "DEVICE_OBJECT is 0xB8 bytes on NT 4.0");

#define IRP_MJ_CREATE                   0x00
#define IRP_MJ_CLOSE                    0x02
#define IRP_MJ_FLUSH_BUFFERS            0x09
#define IRP_MJ_QUERY_VOLUME_INFORMATION 0x0A
#define IRP_MJ_DEVICE_CONTROL           0x0E
#define IRP_MJ_INTERNAL_DEVICE_CONTROL  0x0F
#define IRP_MJ_SHUTDOWN                 0x10
#define IRP_MJ_MAXIMUM_FUNCTION         0x1B

/* NT 4.0's DRIVER_OBJECT, 0xA8 bytes.  The dispatch table holds { entry, toc } descriptors, so
 * every routine stored there goes through DEFINE_DESC (nt.h) -- see docs/STORY.md wall 49.
 * VERIFIED: DriverName at 0x1C is what videoprt reads for its `\Driver\Vga` test; DriverStartIo
 * 0x30, DriverUnload 0x34 and MajorFunction at 0x38 are where kbdclass.sys's and floppy.sys's
 * DriverEntry store their routines (kbdclass 0x123cc: CREATE 0x38, CLOSE 0x40, READ 0x44,
 * FLUSH_BUFFERS 0x5C, DEVICE_CONTROL 0x70, CLEANUP 0x80). */
typedef struct _DRIVER_OBJECT {
    CSHORT Type, Size;                  /* 0x00 */
    PDEVICE_OBJECT DeviceObject;        /* 0x04 */
    ULONG Flags;                        /* 0x08 */
    PVOID DriverStart;                  /* 0x0C */
    ULONG DriverSize;                   /* 0x10 */
    PVOID DriverSection;                /* 0x14 */
    PVOID DriverExtension;              /* 0x18 */
    UNICODE_STRING DriverName;          /* 0x1C */
    PUNICODE_STRING HardwareDatabase;   /* 0x24 */
    PVOID FastIoDispatch;               /* 0x28 */
    PVOID DriverInit;                   /* 0x2C */
    PVOID DriverStartIo;                /* 0x30 */
    PVOID DriverUnload;                 /* 0x34 */
    PVOID MajorFunction[IRP_MJ_MAXIMUM_FUNCTION + 1];   /* 0x38 */
} DRIVER_OBJECT;
_Static_assert(sizeof(DRIVER_OBJECT) == 0xA8, "DRIVER_OBJECT is 0xA8 bytes on NT 4.0");

/* NT 4.0's IO_STACK_LOCATION, 0x24 bytes.  The parameter union is 4-byte packed in NT's own
 * headers, which is why ByteOffset is two ULONGs here and not a LARGE_INTEGER the compiler would
 * align to 8.  VERIFIED: IoInitializeIrp sizes the stack as StackCount * 0x24 (0x800384e8);
 * IoBuildDeviceIoControlRequest (0x800373ec..0x8003762c) writes MajorFunction at 0, Output- and
 * InputBufferLength at 4 and 8, IoControlCode at 0xC, Type3InputBuffer at 0x10;
 * IofCompleteRequest reads Control at 3, DeviceObject at 0x14, CompletionRoutine 0x1C, Context
 * 0x20; i8042prt.sys reads IoControlCode at 0xC and CONNECT_DATA from 0x10 (0x10ff0). */
typedef struct _IO_STACK_LOCATION {
    UCHAR MajorFunction, MinorFunction, Flags, Control;    /* 0x00 */
    union {
        struct { ULONG Length; ULONG Key; ULONG ByteOffsetLow; ULONG ByteOffsetHigh; } Read;
        struct { ULONG OutputBufferLength; ULONG InputBufferLength; ULONG IoControlCode; PVOID Type3InputBuffer; } DeviceIoControl;
        ULONG Raw[4];
    } Parameters;                                          /* 0x04 */
    PDEVICE_OBJECT DeviceObject;                           /* 0x14 */
    PVOID FileObject;                                      /* 0x18 */
    PVOID CompletionRoutine;                               /* 0x1C */
    PVOID Context;                                         /* 0x20 */
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;
_Static_assert(sizeof(IO_STACK_LOCATION) == 0x24, "IO_STACK_LOCATION is 0x24 bytes on NT 4.0");

/* NT 4.0's IRP, 0x70 bytes.  VERIFIED: IoInitializeIrp (0x800384ac..0x800384f4) writes Type 6 at
 * 0, Size at 2, StackCount 0x22, CurrentLocation 0x23, ApcEnvironment 0x26, and
 * CurrentStackLocation 0x60 = irp + 0x70 + StackCount * 0x24; IoBuildDeviceIoControlRequest
 * writes MdlAddress 4, Flags 8, SystemBuffer 0xC, UserIosb 0x28, UserEvent 0x2C, UserBuffer
 * 0x3C and Tail.Overlay.Thread 0x50; IofCompleteRequest takes IoStatus at 0x18. */
typedef struct _IRP {
    CSHORT Type; USHORT Size;           /* 0x00 */
    PMDL MdlAddress;                    /* 0x04 */
    ULONG Flags;                        /* 0x08 */
    PVOID SystemBuffer;                 /* 0x0C  AssociatedIrp.SystemBuffer */
    LIST_ENTRY ThreadListEntry;         /* 0x10 */
    IO_STATUS_BLOCK IoStatus;           /* 0x18 */
    CHAR RequestorMode; BOOLEAN PendingReturned; CHAR StackCount; CHAR CurrentLocation;   /* 0x20 */
    BOOLEAN Cancel; UCHAR CancelIrql; CHAR ApcEnvironment; UCHAR AllocationFlags;          /* 0x24 */
    PIO_STATUS_BLOCK UserIosb;          /* 0x28 */
    PKEVENT UserEvent;                  /* 0x2C */
    ULONG Overlay[2];                   /* 0x30 */
    PVOID CancelRoutine;                /* 0x38 */
    PVOID UserBuffer;                   /* 0x3C */
    UCHAR TailHead[0x20];               /* 0x40  DeviceQueueEntry/DriverContext, Thread, AuxiliaryBuffer, ListEntry */
    PIO_STACK_LOCATION CurrentStackLocation;   /* 0x60 */
    PVOID OriginalFileObject;           /* 0x64 */
    UCHAR TailRest[8];                  /* 0x68  the KAPC alternative runs to 0x70 */
} IRP;
_Static_assert(sizeof(IRP) == 0x70, "IRP is 0x70 bytes on NT 4.0");

#define IoGetCurrentIrpStackLocation(Irp) ((Irp)->CurrentStackLocation)

#define MDL_MAPPED_TO_SYSTEM_VA      0x0001
#define MDL_SOURCE_IS_NONPAGED_POOL  0x0004
static inline PVOID MmGetSystemAddressForMdl(PMDL Mdl);

/* ---- the class-driver interfaces -------------------------------------------------------- */

/* VERIFIED: kbdclass.sys steps its input queue by 0xC (divwu/mulli 0xc, 0x10e88); i8042prt.sys
 * fills UnitId/MakeCode/Flags at 0/2/4 and ExtraInformation at 8 (0x10edc..0x11094). */
typedef struct _KEYBOARD_INPUT_DATA { USHORT UnitId, MakeCode, Flags, Reserved; ULONG ExtraInformation; } KEYBOARD_INPUT_DATA, *PKEYBOARD_INPUT_DATA;
_Static_assert(sizeof(KEYBOARD_INPUT_DATA) == 12, "KEYBOARD_INPUT_DATA is 12 bytes");
#define KEY_MAKE  0
#define KEY_BREAK 1
#define KEY_E0    2

/* VERIFIED: mouclass.sys steps by 0x18 (0x10e80); i8042prt.sys writes Flags at 2, ButtonFlags
 * at 4, LastX/LastY at 0xC/0x10 and ExtraInformation at 0x14 (0x11c18..0x11c28). */
typedef struct _MOUSE_INPUT_DATA { USHORT UnitId, Flags, ButtonFlags, ButtonData; ULONG RawButtons; LONG LastX, LastY; ULONG ExtraInformation; } MOUSE_INPUT_DATA, *PMOUSE_INPUT_DATA;
_Static_assert(sizeof(MOUSE_INPUT_DATA) == 24, "MOUSE_INPUT_DATA is 24 bytes");
#define MOUSE_MOVE_RELATIVE      0
#define MOUSE_LEFT_BUTTON_DOWN   0x0001
#define MOUSE_LEFT_BUTTON_UP     0x0002
#define MOUSE_RIGHT_BUTTON_DOWN  0x0004
#define MOUSE_RIGHT_BUTTON_UP    0x0008

/* What kbdclass / mouclass hand a port driver on connect: their device, and the descriptor of
 * the routine that takes input packets -- called, per the DDK, at DISPATCH_LEVEL.  VERIFIED:
 * kbdclass builds IOCTL 0xB0203 with an 8-byte input buffer (0x12db4), i8042prt checks
 * InputBufferLength == 8 and copies the two words from Type3InputBuffer (0x10fd8..0x1100c). */
typedef struct _CONNECT_DATA { PDEVICE_OBJECT ClassDeviceObject; PVOID ClassService; } CONNECT_DATA, *PCONNECT_DATA;

/* VERIFIED sizes, from i8042prt.sys's buffer-length checks: KEYBOARD_ATTRIBUTES 0x1C (0x10dc8),
 * typematic parameters 6 (0x10e30), KEYBOARD_INDICATOR_PARAMETERS 4 (0x10e8c),
 * MOUSE_ATTRIBUTES 0xC (0x11060). */
typedef struct _KEYBOARD_ATTRIBUTES {
    UCHAR KeyboardIdentifierType, KeyboardIdentifierSubtype;
    USHORT KeyboardMode, NumberOfFunctionKeys, NumberOfIndicators, NumberOfKeysTotal;
    ULONG InputDataQueueLength;
    struct { USHORT UnitId, Rate, Delay; } KeyRepeatMinimum, KeyRepeatMaximum;
} KEYBOARD_ATTRIBUTES;
typedef struct _KEYBOARD_INDICATOR_PARAMETERS { USHORT UnitId, LedFlags; } KEYBOARD_INDICATOR_PARAMETERS;
typedef struct _MOUSE_ATTRIBUTES { USHORT MouseIdentifier, NumberOfButtons, SampleRate, Pad; ULONG InputDataQueueLength; } MOUSE_ATTRIBUTES;

/* CTL_CODE(type, function, method, access) = (type << 16) | (access << 14) | (function << 2) | method
 * VERIFIED: every keyboard code below is built by i8042prt.sys (lis 0xb / ori), the two connects
 * by kbdclass.sys and mouclass.sys, and IS_WRITABLE, CHECK_VERIFY and FORMAT_TRACKS by floppy.sys. */
#define IOCTL_INTERNAL_KEYBOARD_CONNECT     0x000B0203
#define IOCTL_INTERNAL_KEYBOARD_DISCONNECT  0x000B0403
#define IOCTL_INTERNAL_KEYBOARD_ENABLE      0x000B0803
#define IOCTL_INTERNAL_KEYBOARD_DISABLE     0x000B1003
#define IOCTL_KEYBOARD_QUERY_ATTRIBUTES     0x000B0000
#define IOCTL_KEYBOARD_SET_TYPEMATIC        0x000B0004
#define IOCTL_KEYBOARD_SET_INDICATORS       0x000B0008
#define IOCTL_KEYBOARD_QUERY_TYPEMATIC      0x000B0020
#define IOCTL_KEYBOARD_QUERY_INDICATORS     0x000B0040
#define IOCTL_KEYBOARD_QUERY_INDICATOR_TRANSLATION 0x000B0080
#define IOCTL_INTERNAL_MOUSE_CONNECT        0x000F0203
#define IOCTL_INTERNAL_MOUSE_DISCONNECT     0x000F0403
#define IOCTL_INTERNAL_MOUSE_ENABLE         0x000F0803
#define IOCTL_INTERNAL_MOUSE_DISABLE        0x000F1003
#define IOCTL_MOUSE_QUERY_ATTRIBUTES        0x000F0000
#define IOCTL_DISK_GET_DRIVE_GEOMETRY       0x00070000
#define IOCTL_DISK_GET_PARTITION_INFO       0x00074004
#define IOCTL_DISK_IS_WRITABLE              0x00070024
#define IOCTL_DISK_VERIFY                   0x00070014
#define IOCTL_DISK_FORMAT_TRACKS            0x0007C018
#define IOCTL_DISK_CHECK_VERIFY             0x00074800
#define IOCTL_DISK_GET_MEDIA_TYPES          0x00070C00

/* VERIFIED: floppy.sys checks the geometry buffer against 0x18 (0x13a30) and fills 0x200 for
 * BytesPerSector. */
typedef struct _DISK_GEOMETRY { LARGE_INTEGER Cylinders; ULONG MediaType, TracksPerCylinder, SectorsPerTrack, BytesPerSector; } DISK_GEOMETRY, *PDISK_GEOMETRY;
_Static_assert(sizeof(DISK_GEOMETRY) == 24, "DISK_GEOMETRY is 24 bytes");
#define F3_1Pt44_512 2

#define FILE_DEVICE_DISK        0x07
#define FILE_DEVICE_KEYBOARD    0x0B
#define FILE_DEVICE_MOUSE       0x0F
#define FILE_REMOVABLE_MEDIA    0x01
#define FILE_FLOPPY_DISKETTE    0x04
#define DO_BUFFERED_IO          0x04
#define DO_DIRECT_IO            0x10
#define DO_DEVICE_INITIALIZING  0x80
#define IO_NO_INCREMENT         0

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS                 0x00000000
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER       0xC000000D
#endif
#ifndef STATUS_NO_SUCH_DEVICE
#define STATUS_NO_SUCH_DEVICE          0xC000000E
#endif
#ifndef STATUS_INVALID_DEVICE_REQUEST
#define STATUS_INVALID_DEVICE_REQUEST  0xC0000010
#endif
#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL        0xC0000023
#endif
#ifndef STATUS_INSUFFICIENT_RESOURCES
#define STATUS_INSUFFICIENT_RESOURCES  0xC000009A
#endif
#ifndef STATUS_DEVICE_NOT_READY
#define STATUS_DEVICE_NOT_READY        0xC00000A3
#endif
#ifndef STATUS_IO_DEVICE_ERROR
#define STATUS_IO_DEVICE_ERROR         0xC0000185
#endif

/* ---- kernel and HAL routines this driver imports (adbport.imports) ---------------------- */
NTSTATUS IoCreateDevice(PDRIVER_OBJECT DriverObject, ULONG ExtensionSize, PUNICODE_STRING Name,
                        ULONG DeviceType, ULONG Characteristics, BOOLEAN Exclusive, PDEVICE_OBJECT *Out);
VOID IoDeleteDevice(PDEVICE_OBJECT DeviceObject);
VOID IofCompleteRequest(PIRP Irp, CHAR PriorityBoost);
PCONFIGURATION_INFORMATION IoGetConfigurationInformation(VOID);
#define RTL_REGISTRY_DEVICEMAP 4
#define REG_SZ 1
NTSTATUS RtlWriteRegistryValue(ULONG RelativeTo, const WCHAR *Path, const WCHAR *ValueName, ULONG ValueType,
                               PVOID ValueData, ULONG ValueLength);
VOID KeAcquireSpinLock(PKSPIN_LOCK Lock, PKIRQL OldIrql);
VOID KeReleaseSpinLock(PKSPIN_LOCK Lock, KIRQL OldIrql);
PVOID MmMapLockedPages(PMDL Mdl, CHAR AccessMode);
VOID RtlZeroMemory(PVOID Dst, ULONG Length);
VOID RtlMoveMemory(PVOID Dst, const VOID *Src, ULONG Length);
BOOLEAN HalPxiCommandAdb(UCHAR Command, PUCHAR Data, UCHAR Length, BOOLEAN Poll);
VOID HalPxiAdbSetCallback(PVOID CallbackDescriptor);
VOID HalPxiAdbAutopoll(USHORT Mask);
BOOLEAN HalAnsOemDiskQuery(PULONG PhysicalBase, PULONG Bytes);
VOID HalDisplayString(PCHAR String);
/* thunk.S: call a { entry, toc } descriptor with four arguments -- how the class service is reached */
ULONG AdbCallDesc4(PVOID Desc, ULONG a1, ULONG a2, ULONG a3, ULONG a4);

/* MDL (nt.h): VERIFIED against MmMapLockedPages (0x8004df94..0x8004e128): MdlFlags at 6, where it
 * sets MDL_MAPPED_TO_SYSTEM_VA (1); MappedSystemVa at 0xC, which it writes; StartVa, ByteCount
 * and ByteOffset at 0x10, 0x14, 0x18, which it reads. */
static inline PVOID MmGetSystemAddressForMdl(PMDL Mdl)
{
    if (Mdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL)) return Mdl->MappedSystemVa;
    return MmMapLockedPages(Mdl, KernelMode);
}

/* ---- this driver ------------------------------------------------------------------------- */
typedef enum { AdbDeviceKeyboard = 1, AdbDevicePointer = 2, AdbDeviceOemDisk = 3 } ADB_DEVICE_KIND;

typedef struct _ADB_EXTENSION {
    ADB_DEVICE_KIND Kind;
    PDEVICE_OBJECT Self;
    /* keyboard and pointer: who to hand input to, once the class driver has connected */
    CONNECT_DATA Connect;
    BOOLEAN Enabled;
    KSPIN_LOCK Lock;
    ULONG Packets, Delivered;           /* counters, for the trace */
    /* OEM disk */
    PUCHAR Image;
    ULONG ImageBytes;
} ADB_EXTENSION, *PADB_EXTENSION;

extern PADB_EXTENSION AdbKeyboard, AdbPointer;
VOID AdbLog(const char *fmt, ...);
/* per-device dispatch, each in its own file */
NTSTATUS AdbKeyboardControl(PADB_EXTENSION Ext, PIRP Irp, PIO_STACK_LOCATION Sp);
VOID AdbKeyboardPacket(PADB_EXTENSION Ext, const UCHAR *Data, ULONG Length);
NTSTATUS AdbPointerControl(PADB_EXTENSION Ext, PIRP Irp, PIO_STACK_LOCATION Sp);
VOID AdbPointerPacket(PADB_EXTENSION Ext, const UCHAR *Data, ULONG Length);
NTSTATUS AdbDiskInitialize(PADB_EXTENSION Ext);
NTSTATUS AdbDiskReadWrite(PADB_EXTENSION Ext, PIRP Irp, PIO_STACK_LOCATION Sp);
NTSTATUS AdbDiskControl(PADB_EXTENSION Ext, PIRP Irp, PIO_STACK_LOCATION Sp);
