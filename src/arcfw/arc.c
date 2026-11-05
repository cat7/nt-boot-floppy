/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * arc.c -- the system parameter block, the firmware vector, and the ARC services that are not
 * a module of their own.  Written from the ARC specification; the layout of the block and the
 * vector's 37 slots are as the veneer laid them out at 0x4000 (read from memory on 5 November:
 * signature 'ARCS', length 0x3C, version 2.0, restart block at 0x40D0, vector at 0x403C with
 * descriptor pointers in it, no vendor vector). */
#include "arcfw.h"
#include "of.h"

SYSTEM_PARAMETER_BLOCK *arc_spb;
FIRMWARE_VECTOR arc_vector;
static RESTART_BLOCK *restart_block;

#define DESC(n) extern ULONG desc_##n[2];
DESC(Load) DESC(Invoke) DESC(Execute) DESC(Halt) DESC(PowerDown) DESC(Restart) DESC(Reboot) DESC(EnterInteractiveMode)
DESC(GetPeer) DESC(GetChild) DESC(GetParent) DESC(GetConfigurationData) DESC(AddChild) DESC(DeleteComponent)
DESC(GetComponent) DESC(SaveConfiguration) DESC(GetSystemId) DESC(GetMemoryDescriptor) DESC(GetTime)
DESC(GetRelativeTime) DESC(GetDirectoryEntry) DESC(Open) DESC(Close) DESC(Read) DESC(GetReadStatus) DESC(Write)
DESC(Seek) DESC(Mount) DESC(GetEnvironmentVariable) DESC(SetEnvironmentVariable) DESC(GetFileInformation)
DESC(SetFileInformation) DESC(FlushAllCaches) DESC(TestUnicodeCharacter) DESC(GetDisplayStatus)

VOID arc_init_spb(VOID)
{
    /* Page 4: the block, the vector right after it (0x403C), the restart block after that
     * (0x40D0), all inside the page the boot script maps (ledger row 1's claim, done in Forth). */
    arc_spb = (SYSTEM_PARAMETER_BLOCK *)ARC_SPB_PHYS;
    memset(arc_spb, 0, 0x200);
    arc_spb->Signature = ARC_SYSTEM_BLOCK_SIGNATURE;
    arc_spb->Length = 0x3C;
    arc_spb->Version = 2;
    arc_spb->Revision = 0;
    PVOID *vec = (PVOID *)(ARC_SPB_PHYS + 0x3C);
    arc_spb->FirmwareVectorLength = FIRMWARE_VECTOR_ENTRIES * 4;
    arc_spb->FirmwareVector = KSEG0(vec);
    arc_spb->VendorVectorLength = 0;
    arc_spb->VendorVector = NULL;
    PVOID *v = (PVOID *)&arc_vector;
    v[0] = desc_Load; v[1] = desc_Invoke; v[2] = desc_Execute; v[3] = desc_Halt; v[4] = desc_PowerDown;
    v[5] = desc_Restart; v[6] = desc_Reboot; v[7] = desc_EnterInteractiveMode; v[8] = NULL;
    v[9] = desc_GetPeer; v[10] = desc_GetChild; v[11] = desc_GetParent; v[12] = desc_GetConfigurationData;
    v[13] = desc_AddChild; v[14] = desc_DeleteComponent; v[15] = desc_GetComponent; v[16] = desc_SaveConfiguration;
    v[17] = desc_GetSystemId; v[18] = desc_GetMemoryDescriptor; v[19] = NULL; v[20] = desc_GetTime;
    v[21] = desc_GetRelativeTime; v[22] = desc_GetDirectoryEntry; v[23] = desc_Open; v[24] = desc_Close;
    v[25] = desc_Read; v[26] = desc_GetReadStatus; v[27] = desc_Write; v[28] = desc_Seek; v[29] = desc_Mount;
    v[30] = desc_GetEnvironmentVariable; v[31] = desc_SetEnvironmentVariable; v[32] = desc_GetFileInformation;
    v[33] = desc_SetFileInformation; v[34] = desc_FlushAllCaches; v[35] = desc_TestUnicodeCharacter;
    v[36] = desc_GetDisplayStatus;
    for (ULONG i = 0; i < FIRMWARE_VECTOR_ENTRIES; i++) vec[i] = v[i];
    /* the restart block: one processor, signature 'RSTB', nothing saved */
    restart_block = (RESTART_BLOCK *)(ARC_SPB_PHYS + 0xD0);
    restart_block->Signature = ARC_RESTART_BLOCK_SIGNATURE;
    restart_block->Length = 0x1C0;
    restart_block->Version = 1; restart_block->Revision = 2;
    restart_block->NextRestartBlock = NULL;
    restart_block->RestartAddress = NULL;
    restart_block->BootMasterId = 0;
    restart_block->ProcessorId = 0;
    restart_block->BootStatus = 0x14;           /* ProcessorReady | ProcessorStart, the veneer's value */
    restart_block->SaveAreaLength = 0x1C0 - 0x30;
    arc_spb->RestartBlock = KSEG0(restart_block);
    ULONG sum = 0;
    for (ULONG i = 0; i < restart_block->Length / 4; i++) sum += ((PULONG)restart_block)[i];
    restart_block->CheckSum = (ULONG)(-(LONG)sum);
}

/* ---- the services ---------------------------------------------------------------------- */

ARC_STATUS arc_Load(PCHAR path, ULONG top, PULONG entry, PULONG low)
{
    PVOID e, lo, hi;
    DBG(DBG_LOAD, "arcfw: Load('%s', top %x)\n", path, top);
    ARC_STATUS s = load_image(path, &e, &lo, &hi);
    if (s == ESUCCESS) { *entry = (ULONG)e; *low = (ULONG)lo; }
    return s;
}
ARC_STATUS arc_Invoke(ULONG entry, ULONG stack, ULONG argc, PCHAR *argv, PCHAR *envp)
{
    (void)stack;
    if ((entry & 3) || (stack & 3)) return EFAULT;
    load_run((PVOID)entry, argc, argv, envp);
}
ARC_STATUS arc_Execute(PCHAR path, ULONG argc, PCHAR *argv, PCHAR *envp)
{
    PVOID e, lo, hi;
    ARC_STATUS s = load_image(path, &e, &lo, &hi);
    if (s != ESUCCESS) return s;
    load_run(e, argc, argv, envp);
}
VOID arc_Halt(VOID) { con_printf("\narcfw: Halt\n"); of_exit(); }
VOID arc_PowerDown(VOID) { con_printf("\narcfw: PowerDown\n"); of_interpret("shut-down"); of_exit(); }
VOID arc_Restart(VOID) { con_printf("\narcfw: Restart\n"); of_interpret("reset-all"); of_exit(); }
VOID arc_Reboot(VOID) { con_printf("\narcfw: Reboot\n"); of_interpret("reset-all"); of_exit(); }
VOID arc_EnterInteractiveMode(VOID) { con_printf("\narcfw: EnterInteractiveMode\n"); of_enter(); }

PCONFIGURATION_COMPONENT arc_GetPeer(PCONFIGURATION_COMPONENT c)
{
    PARC_NODE n = config_from_component(c);
    PARC_NODE r = n ? n->Peer : NULL;
    DBG(DBG_CONFIG, "arcfw: GetPeer(%p) -> %p\n", c, r ? &r->Component : NULL);
    return r ? &r->Component : NULL;
}
PCONFIGURATION_COMPONENT arc_GetChild(PCONFIGURATION_COMPONENT c)
{
    PARC_NODE r = c ? (config_from_component(c) ? config_from_component(c)->Child : NULL) : config_root();
    DBG(DBG_CONFIG, "arcfw: GetChild(%p) -> %p\n", c, r ? &r->Component : NULL);
    return r ? &r->Component : NULL;
}
PCONFIGURATION_COMPONENT arc_GetParent(PCONFIGURATION_COMPONENT c)
{
    PARC_NODE n = config_from_component(c);
    PARC_NODE r = n ? n->Parent : NULL;
    return r ? &r->Component : NULL;
}
ARC_STATUS arc_GetConfigurationData(PVOID buf, PCONFIGURATION_COMPONENT c)
{
    PARC_NODE n = config_from_component(c);
    if (!n) return EINVAL;
    DBG(DBG_CONFIG, "arcfw: GetConfigurationData(%p) -> %u bytes\n", c, n->Component.ConfigurationDataLength);
    if (n->Component.ConfigurationDataLength && n->ConfigurationData)
        memcpy(buf, n->ConfigurationData, n->Component.ConfigurationDataLength);
    return ESUCCESS;
}
PCONFIGURATION_COMPONENT arc_AddChild(PCONFIGURATION_COMPONENT parent, PCONFIGURATION_COMPONENT c, PVOID data)
{
    (void)parent; (void)c; (void)data;
    return NULL;                                  /* the tree is the firmware's; EACCES in spirit */
}
ARC_STATUS arc_DeleteComponent(PCONFIGURATION_COMPONENT c) { (void)c; return EACCES; }
PCONFIGURATION_COMPONENT arc_GetComponent(PCHAR path)
{
    const CHAR *rest;
    PARC_NODE n = config_lookup(path, &rest);
    DBG(DBG_CONFIG, "arcfw: GetComponent('%s') -> %p\n", path, n ? &n->Component : NULL);
    return n ? &n->Component : NULL;
}
ARC_STATUS arc_SaveConfiguration(VOID) { return ESUCCESS; }
ARC_STATUS arc_GetSystemId(PSYSTEM_ID id)
{
    memcpy(id->VendorId, "Apple   ", 8);
    memcpy(id->ProductId, "ANS500  ", 8);
    return ESUCCESS;
}
PMEMORY_DESCRIPTOR arc_GetMemoryDescriptor(PMEMORY_DESCRIPTOR prev)
{
    PMEMORY_DESCRIPTOR d = memory_next(prev);
    DBG(DBG_MEM, "arcfw: GetMemoryDescriptor(%p) -> %p%s\n", prev, d, d ? "" : " (end)");
    return d;
}

/* Time: Open Firmware's milliseconds for the relative clock; the wall clock from the rtc
 * package when it has get-time, else a fixed date -- the HAL reads Cuda's clock itself. */
static ULONG t0;
PTIME_FIELDS arc_GetTime(VOID)
{
    static TIME_FIELDS tf;
    ULONG out[6];
    if (of_interpret("get-time") == 0) { /* leaves sec min hour day month year on the stack: not fetchable this way */ }
    (void)out;
    tf.Year = 2026; tf.Month = 11; tf.Day = 5; tf.Hour = 12; tf.Minute = 0; tf.Second = 0; tf.Milliseconds = 0; tf.Weekday = 4;
    return &tf;
}
ULONG arc_GetRelativeTime(VOID)
{
    if (!t0) t0 = of_milliseconds();
    return (of_milliseconds() - t0) / 1000;
}

ARC_STATUS arc_GetDirectoryEntry(ULONG fd, PDIRECTORY_ENTRY e, ULONG n, PULONG count) { (void)fd; (void)e; (void)n; *count = 0; return ENOTDIR; }
ARC_STATUS arc_Open(PCHAR path, OPEN_MODE mode, PULONG fd) { return io_open(path, mode, fd); }
ARC_STATUS arc_Close(ULONG fd) { return io_close(fd); }
ARC_STATUS arc_Read(ULONG fd, PVOID buf, ULONG n, PULONG count) { return io_read(fd, buf, n, count); }
ARC_STATUS arc_GetReadStatus(ULONG fd) { return io_get_read_status(fd); }
ARC_STATUS arc_Write(ULONG fd, PVOID buf, ULONG n, PULONG count) { return io_write(fd, buf, n, count); }
ARC_STATUS arc_Seek(ULONG fd, LARGE_INTEGER *off, SEEK_MODE mode) { return io_seek(fd, off, mode); }
ARC_STATUS arc_Mount(PCHAR path, MOUNT_OPERATION op) { (void)path; (void)op; return ESUCCESS; }
/* The vector's GetEnvironmentVariable is the ARC one: it returns the value, or NULL when there
 * is none.  (The three-argument status-returning form is the kernel's HalGetEnvironmentVariable,
 * not this.)  A first arcfw returned ENOENT here; OSLOADER took 2 for a string, read 'HPTSIZE'
 * as set, sized the hashed page table for a small machine and the kernel died with bugcheck 0x50
 * at user-mode start (5 November). */
PCHAR arc_GetEnvironmentVariable(PCHAR name)
{
    const CHAR *v = env_lookup(name);
    DBG(DBG_ENV, "arcfw: GetEnvironmentVariable('%s') -> %s\n", name, v ? v : "(none)");
    return (PCHAR)v;
}
ARC_STATUS arc_SetEnvironmentVariable(PCHAR name, PCHAR value) { return env_set(name, value); }
ARC_STATUS arc_GetFileInformation(ULONG fd, PFILE_INFORMATION fi) { return io_get_file_information(fd, fi); }
ARC_STATUS arc_SetFileInformation(ULONG fd, ULONG attr_set, ULONG attr_clear) { (void)fd; (void)attr_set; (void)attr_clear; return EROFS; }
VOID arc_FlushAllCaches(VOID) { extern VOID cache_sweep_all(VOID); cache_sweep_all(); }
ARC_STATUS arc_TestUnicodeCharacter(ULONG fd, USHORT c) { (void)fd; return c < 0x80 ? ESUCCESS : EINVAL; }
PARC_DISPLAY_STATUS arc_GetDisplayStatus(ULONG fd)
{
    static ARC_DISPLAY_STATUS ds = { 0, 0, 79, 24, 7, 0, 0, 0, 0 };
    (void)fd;
    return &ds;
}
