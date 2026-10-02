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
