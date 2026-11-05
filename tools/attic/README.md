<!-- SPDX-License-Identifier: GPL-2.0-only -->
<!-- Copyright (C) 2026 powermac-nt-hal contributors -->

# attic — tools from the patched-CD era

Until 15 September 2026 this project delivered its HAL by **editing the user's NT 4.0 CD image**:
a HAL written over `\PPC\HALEAGLE.DLL`, a rewritten `TXTSETUP.SIF`, a keyboard driver written
over `\PPC\I8042PRT.SYS`, and the veneer staged on a separate disk. The boot floppy
([`docs/2026-09-15-the-boot-floppy.md`](../../docs/2026-09-15-the-boot-floppy.md)) retired all of
that, and these five tools with it. They are kept because the session write-ups and `STORY.md`
cite them, and because the measurements they encode (where `\PPC` sits on the disc, what Setup
checksums, how the firmware loads the veneer off a CD by name) are still true.

| tool | what it did |
|---|---|
| `patch-iso.py` | overwrote one file's extent in an ISO 9660 image in place — how `hal.dll` first reached Setup, over `HALEAGLE.DLL` |
| `mkoem.py` | rewrote `TXTSETUP.SIF` (same length) and the `\PPC` directory record so Setup offered *Apple Network Server 500/700* and installed `halshinr.dll` — the HAL's own identity, before `txtsetup.oem` on the floppy did the same job without touching the disc |
| `mkpatchediso.py` | one command from a stock CD to a patched ISO plus a staging image holding the patched veneer |
| `mkbootcd.py` | prototype: one CD that boots Setup with the veneer loaded off the CD by name, no staging disk |
| `mkcoldboot.py` | the emulator script that booted Setup from a cold machine and a patched CD, with every patch site read back out of guest memory |

They are not run by `make`, not covered by CI's checks, and not maintained. They still find the
live tools they call (`mkveneer.py`, `mkbootscript.py`) one directory up.
