# VitaChipsChallenge

A clean source reconstruction and native PS Vita port of the Microsoft Windows
3.x version of **Chip's Challenge**.

## Status

Reverse engineering has started from the original 16-bit Windows NE executable.
The repository currently provides a reproducible reference-extraction and
executable-inventory pipeline. It is not yet a playable release.

This project does not use Tile World or another clone as its gameplay engine.
Every reconstructed subsystem will be tied to behavior or code observed in the
reference executable and verified independently.

## Reference build

| File | SHA-256 |
|---|---|
| `chips_challenge.zip` | `ffbb83dc4ca5cc9e8cbf78271b44a42ea4e7db2f4f8d1953383390acf94e7ddf` |
| `CHIPS.EXE` | `8e26acd67cf120bd5b512de4b4e78b80aca1579413cd04f3b2b68909a375866c` |
| `CHIPS.DAT` | `b0a68f21642447385512e0ed9386a4a13a4f7e9a38f909da772104d8e6b5c1fb` |
| `WEP4UTIL.DLL` | `40a405813946caac1aa2ec3e31fa570de1c418f8db43182d614e0bac4e1978cb` |

The original files are not distributed by this repository. See
[`reference/README.md`](reference/README.md) for local setup.

## Target

The Vita is little-endian ARMv7-A with a 32-bit ILP32 ABI. The original program
is 16-bit x86 code using the Windows 3.x NE segmented executable format. The
port translates reconstructed game logic into portable fixed-width C and uses
a Vita-native platform layer for rendering, input, audio, storage, and lifecycle
events.

## Roadmap

1. Inventory NE segments, resources, imports, exports, and relocation records.
2. Reconstruct DAT loading, game state, tile interactions, timing, and movement.
3. Match reference behavior with host-side differential fixtures.
4. Implement Vita graphics, controls, audio, persistence, and LiveArea assets.
5. Validate ARMv7-A ABI attributes and complete physical Vita/PSTV testing.

No gold release will be declared until the reconstructed engine passes the
behavior corpus and the exact VPK passes the physical-device checklist.

