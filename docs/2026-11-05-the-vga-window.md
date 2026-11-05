<!-- SPDX-License-Identifier: GPL-2.0-only -->
<!-- Copyright (C) 2026 powermac-nt-hal contributors -->

# The legacy VGA window, translated: a stock `cirrus.sys` boots to the desktop

*5 November 2026. Ledger row 6 patched one entry in Microsoft's display miniport so that its
claim on the legacy VGA window named an address the HAL would translate. The HAL now translates
the address the miniport asks for, and the patch, the OEM display class that carried it, and the
two Microsoft files on the floppy that came with it are retired. The floppy is eight files; the
only Microsoft bytes on it are the veneer's.*

## 1. What row 6 was

`cirrus.sys` hands `videoprt` a table of access ranges, and one of them is the legacy VGA window,
`0xA0000` for 128 KB. `VideoPortVerifyAccessRanges` has `IoReportResourceUsage` translate every
range through `HalTranslateBusAddress`, and this HAL refused PCI memory below `0x80000000` because
on this board those CPU addresses are RAM. The kernel turned the refusal into
`STATUS_INVALID_PARAMETER`, the miniport gave up, and text-mode Setup stopped with *"fatal error
while initializing your computer's video"*: wall 25
([`2026-09-15-the-boot-floppy.md`](2026-09-15-the-boot-floppy.md) §4.3).

The fix of 15 September moved the table entry to `0x90000000`, an address nothing decodes, and
delivered the patched file through an OEM `[Display]` class on the floppy, together with
`cirrus.dll`, because an OEM display entry needs both. On 16 and 17 September the HAL learned to
answer that moved range with the 54M30's legacy-window mirror, 128 KB inside BAR0 where the
emulated part decodes banked display memory and, at `$B8000`, the BitBLT registers
([`2026-09-17-the-bitblt-engine.md`](2026-09-17-the-bitblt-engine.md)). The real `0xA0000`
stayed refused, with a comment that said why: it was an address the CPU could not reach on the
PCI bus, and earlier experiments aliasing it had ended badly.

## 2. Why the refusal no longer held

Those experiments were made against raw VRAM, before the mirror existed. What the translation
returns is a CPU-physical address, and the one returned for the moved window is VRAM, not RAM:
the reason for refusing `0xA0000` ("handing it back would let a driver write over the kernel")
does not apply to a translation that hands back the mirror. The two bus ranges mean the same
window, so they should get the same answer. `src/pci.c`:

```c
if ((bus_lo >= VGA_APERTURE_MOVED && bus_lo < VGA_APERTURE_MOVED + 0x20000u) ||
    (bus_lo >= 0xA0000u && bus_lo < 0xC0000u)) {
    ULONG vram = HalpVgaVramPhys();
    if (!vram) return FALSE;
    *TranslatedAddress = vram + VGA_APERTURE_MIRROR + (bus_lo & 0x1FFFFu);
    return TRUE;
}
```

The moved range stays accepted so that floppies built before today, which carry the patched
driver, keep booting.

## 3. The measurement

Everything below ran on the installed-system disk of the emulator's test data
(`winnt_4_0_ppc_ans_512mb.img`, text-mode and GUI Setup complete, Administrator with no
password) and the emulator's own `ans-nt-installed-boot` row, whose goldens are the logon
screen, the Logon Information dialog and the desktop, matched pixel for pixel.

**A stock `cirrus.sys`.** The installed disk carries the patched driver Setup copied off the
floppy. Reversing the patch is the patch backwards: the one table entry back from `0x90000000`
to `0xA0000` at file offset `0xB4A0`, and the PE checksum recomputed. Re-applying
`move_vga_aperture` to the result reproduces the floppy's file byte for byte, so the reversed
file is Microsoft's as shipped. `fatput.py` put it at `\WINNT\SYSTEM32\DRIVERS\CIRRUS.SYS` on a
copy of the disk, and the rebuilt HAL at `\OS\WINNT40\HAL.DLL` on its system partition.

| disk | HAL | result |
|---|---|---|
| stock `cirrus.sys` | translates `0xA0000` | **pass**, all five rungs: kernel, logon screen, Ctrl+Alt+Del, desktop; 83 s |
| stock `cirrus.sys` | previous (refuses `0xA0000`) | kernel up (B3) at the same point, then no logon screen in 130 billion instructions, twelve times what the passing boot needed for the whole desktop |
| patched `cirrus.sys` | translates `0xA0000` | pass, as before: the moved range still answers |

The first row is the result; the second is what row 6 was working around, reproduced on the
same disk with only the HAL changed.

## 4. What it retires

- **Ledger row 6.** No Microsoft driver is modified any more.
- **The OEM `[Display]` class** in `txtsetup.oem`, and with it `cirrus.sys` and `cirrus.dll` on
  the floppy. `mkfloppy.py` takes one file off the CD now, the veneer, and the floppy is eight
  files: two veneers, the HAL, the ADB driver, `txtsetup.oem` and three Open Firmware scripts.
  `mkbootfloppy.py` keeps `--display-driver`, `--display-dll` and `--vga-aperture` as options for
  anyone who wants to try another miniport; nothing uses them.
- **The "cannot be redistributed" story** shrinks to the veneer: two files, both patched
  (rows 1–5 and 17), which an ARC firmware of the project's own would retire in turn.

## 5. What is not yet measured

**Text-mode Setup with the stock driver.** Setup initialises video through the same
`VideoPortVerifyAccessRanges` on the same `cirrus.sys`, now the copy on the CD, so the mechanism
is the one measured above. But the install itself has not been re-run: this session has no NT
CD image. Two things change in the walk when it is. At the video-adapter screen the user takes
the CD's own Cirrus Logic entry instead of *Other* and the floppy's; and the emulator's
`ans-nt-install` row, which types the *Other* path and checks a golden with *Cirrus Logic 54M30
(Apple Network Server 500/700)* in the hardware list, needs its keystrokes and that golden
re-captured against a floppy built from this revision. Both are one run with the CD.

**Real hardware** decodes `0xA0000` on the PCI bus to nothing the CPU can reach, exactly as the
old comment said; what the HAL returns is BAR0's mirror, which exists on a real GD5430 as the
window the chip aliases at the top of its linear aperture. That the emulated mirror and the real
one agree is the emulator model's claim, not this note's.
