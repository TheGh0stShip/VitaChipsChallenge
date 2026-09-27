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
- Metadata uses byte type and byte length fields. Confirmed text fields are
  title (3), XOR-encoded password (6), and hint (7).
- Password bytes are XORed with `0x99`; level 1 decodes to `BDHP`.

The remaining metadata types contain trap links, clone links, and creature
ordering. They are preserved as the next DAT reconstruction milestone rather
than being guessed from third-party source.

## Provenance boundary

The former Tile World adaptation is stored outside this repository at
`VitaChipsChallenge-tworld-reference`. No source from it is copied here. The
original game archive is ignored under `reference/` and is required only for
local analysis and personal builds.

