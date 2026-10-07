# Windows Vita3K validation

Validation uses the installed Windows build at `D:\Vita3K\Vita3K.exe`. WSL is
used for compilation only; the Linux Vita3K build is excluded from this check.

## September 26, 2026 result

- Vita3K `0.2.1 4098-bbd5c362` on Windows 11 installed the VPK as `VITA00001`.
- The retail based bubble, startup card, and LiveArea background rendered.
- Vita3K loaded the packaged game data from `app0:/data/` (a personal build;
  release builds read `ux0:data/VitaChipsChallenge/data/`).
- The application rendered at 960 by 544 between 85 and 108 FPS during capture.
- A posted Windows Up key event moved Chip and the next frame rendered.
- The first run exposed the row-major sprite lookup error. The retest used the
  corrected column-major layout of the original 416 by 512 bitmap.

This emulator result verifies packaging, startup, rendering, and basic input.
It does not replace testing on PS Vita hardware.

## October 3, 2026: hardware colour check

A PS Vita screenshot showed red and blue swapped in every bitmap. The asset
pipeline had swapped them to suit Vita3K's display of 24-bit BMP textures;
hardware renders SDL's BMP colours correctly. The swap was removed, so
Vita3K captures now show red and blue exchanged while the device shows the
original Windows palettes.
