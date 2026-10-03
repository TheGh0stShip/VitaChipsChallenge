# Reconstruction log

## Reference identity

Analysis targets the supplied Microsoft Windows 3.x NE executable with
SHA-256 `8e26acd67cf120bd5b512de4b4e78b80aca1579413cd04f3b2b68909a375866c`.
The executable is 267,776 bytes, expects Windows 3.0, and was linked by NE
linker version 5.30.

Generated inventories are checked in as
[`reference-inventory.json`](reference-inventory.json) and
[`wep4util-inventory.json`](wep4util-inventory.json). They contain offsets and
metadata, not original executable bytes.

## Confirmed executable structure

- `CHIPS.EXE` has nine 16-bit x86 code segments and one automatic data segment.
- Initial execution begins at `1:001A`; the automatic data segment is segment 10.
- The executable imports `KERNEL`, `GDI`, `USER`, and `WEP4UTIL`.
- Ten named window procedures are exported, including `MAINWNDPROC` at
  `2:225C`, `BOARDWNDPROC` at `2:274E`, and `INFOWNDPROC` at `2:2866`.
- The NE resource table contains seven bitmap groups plus icon, menu, dialogs,
  string table, accelerator, resource-data, and cursor entries.
- The executable has 218 relocation records. These identify Windows and
  `WEP4UTIL` calls even though imports are ordinal-based.

## Confirmed DAT structure

The parser in `src/dat.c` is based on direct byte-level inspection of the
supplied `CHIPS.DAT` and is verified against all 149 records.

- File magic is little-endian `0x0002AAAC`, followed by a 16-bit level count.
- Each level is length-prefixed and contains 16-bit number, time limit, chip
  count, and map-format value `1`.
- Two 32 by 32 tile layers use byte RLE. `FF count value` expands a run;
  other bytes are literals.
- Metadata uses byte type and byte length fields. Confirmed fields are title
  (3), trap links (4), clone links (5), XOR-encoded password (6), hint (7),
  and ordered creature positions (10).
- Password bytes are XORed with `0x99`; level 1 decodes to `BDHP`.

All metadata types present in the supplied 149-level file are parsed. Trap
records use five little-endian 16-bit values, clone records use four, and each
ordered creature position is an `(x, y)` byte pair.

## Reconstructed gameplay slice

`src/game.c` is an independent fixed-width C implementation. The current slice
reconstructs map layering, player direction and collision, thin walls, movable
blocks, water-to-dirt conversion, chips, sockets, keys, doors, boots, thief,
fire, water, bombs, fake blue walls, toggle buttons, exits, and the 20 Hz level
timer. The working implementation also includes initial monster movement,
traps, clone links, teleports, ice, force floors, and event-driven WAV effects.
Those systems remain partial because their Win16 movement phases and edge cases
are not yet covered by reference traces. The detailed gate is in
[`COMPATIBILITY.md`](COMPATIBILITY.md).

## Level completion scoring

`src/score.c` reconstructs the `DLG_COMPLETE` `WM_INITDIALOG` handler in
code segment 6 (dialog procedure entry `6:03C9`, init case `6:0422`), located
by its references to the `Time Bonus:  %d` format at `DS:0B83`.

- Time bonus is seconds left times 10 (`6:042B`).
- Level bonus starts at 500 times the level number (`6:0441`). For each failed
  attempt it is multiplied by 4 and long-divided by 5, stopping as soon as it
  first falls below 500; that sub-500 value is kept (`6:0466`-`6:0495`).
- The headline is chosen by attempts: 0, 1-2, 3-4, and 5 or more select the
  strings at `DS:0B34`, `0B47`, `0B56`, and `0B6B` (`6:04A6`).
- A stored record is read only when the level does not exceed the stored
  highest level (`6:0586`) and is ignored if its time or score is negative.
  The saved record keeps the larger time left and larger score separately;
  the running total grows by the score improvement only (`6:05C2`-`6:0613`).
- A time improvement message (`DS:0BED`) takes precedence over a score
  improvement message (`DS:0C22`); otherwise the line is blank. With no
  stored record, the new-record message at `DS:0BB7` is shown.

## Session flow, counters, and sounds

- Death (`2:0B9A`): the message is chosen by death reason `state+0x816`
  (1 fire, 2 water, 3 bomb, 4 block, 5 creature, 6 time) from `DS:0176`
  through `DS:0234`, shown in a message box captioned `DS:0068`, and the
  level reloads as a retry.
- Level loader (`4:0356`): a retry increments attempts (`state+0xA30`). When
  Chip took more than 30 steps (`state+0xA34`, incremented at `7:180F`) and
  the level is not 144 or 149, a trouble counter (`state+0xA32`) also rises.
  At 10 the loader asks `DS:090C` with Yes/No; Yes skips to the next level.
- Clock (`7:05BD`): one second elapses per 10 engine ticks. The Tick sound
  plays at 15 seconds or less; at zero ChipDeathByTime plays and death reason
  6 applies.
- Counters (`2:0CBE`, `2:29A6`, `9:00EA`): see `COMPATIBILITY.md`.
- Sounds (`8:056C`): `sndPlaySound(name, SND_ASYNC | SND_NODEFAULT)`, so a
  new sound replaces the current one. Sound indices and default files are
  listed in `include/vcc/game.h`.
- Dialogs call WEP4UTIL `GRAYDLGPROC` and `CENTERHWND` (ordinals 1202 and
  103), so they have gray clients and are centered on the main window.
- `tools/ne_disasm.py` produces listings with relocations resolved. Its
  output is derived from the reference binary and stays under `reference/`.

## Confirmed interface resources

The `CHIPSMENU` resource at file offset `0x3FC00` proves the original command
set. Four adjacent dialog templates prove the Go To, Password Entry, Best
Times, and Level Complete flows. Their exact captions and current implementation
state are recorded in [`COMPATIBILITY.md`](COMPATIBILITY.md).

## Original graphics pipeline

`tools/extract_ne_resources.py` reads bitmap offsets from the checked-in NE
inventory and wraps each Windows DIB in a standard BMP header. The port uses
the 416 by 512 `OBJ32_4` sheet directly. `tools/build_vita_assets.py` derives
the icon, splash, LiveArea background, and system background from `OBJ32_4`,
`BACKGROUND`, and `CHIPEND`; it introduces no generated or clone artwork.

## Provenance boundary

The former Tile World adaptation is stored outside this repository at
`VitaChipsChallenge-tworld-reference`. No source from it is copied here. The
original game archive is ignored under `reference/` and is required only for
local analysis and personal builds.
