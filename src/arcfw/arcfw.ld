/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 powermac-nt-hal contributors
 *
 * arcfw.ld -- the ARC firmware shim's layout, for elf2pe --raw-coff.  Open Firmware's pe-loader
 * lays the COFF out at its ImageBase and starts at ImageBase + AddressOfEntryPoint, so the image
 * is linked where it runs: 0x50000, the address Microsoft's veneer used and the boot scripts
 * already claim.  pe-loader maps no headers and starts at ImageBase, so .text begins there. */
ENTRY(_start)
SECTIONS
{
  /* RVA 0 is the entry: pe-loader starts the program at ImageBase, header-less (F1 found out) */
  . = 0x00050000;
  .text : { KEEP(*(.text.start)) *(.text .text.*) }
  . = ALIGN(0x1000);
  .rdata : { *(.rodata .rodata.*) }
  . = ALIGN(0x1000);
  .data : { *(.data .data.* .sdata .sdata.* .got2 .got) }
  __bss_start = .;
  .bss  : { *(.bss .bss.* .sbss .sbss.* COMMON) }
  . = ALIGN(16);
  __bss_end = .;
  /DISCARD/ : { *(.comment .note.* .eh_frame .rela.eh_frame .PPC.EMB.apuinfo) }
}
