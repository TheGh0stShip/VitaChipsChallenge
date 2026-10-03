# Windows reference compatibility

This file is the release gate for the clean source reconstruction. A feature is
**confirmed** only when its behavior is tied to the supplied Windows binary,
resource, or data file. **Implemented** means code exists but still needs a
reference behavior corpus. Unlisted behavior must not be assumed compatible.

## Reference interface inventory

The `CHIPSMENU` resource at `CHIPS.EXE` file offset `0x3FC00` contains these
commands and captions:

| Menu | Commands in the Windows resource |
|---|---|
| Game | New Game (`F2`), Pause (`F3`), Best Times, Exit |
| Options | Background Music, Sound Effects, Color |
| Level | Restart (`Ctrl+R`), Next (`Ctrl+N`), Previous (`Ctrl+P`), Go To |
| Help | Contents (`F1`), How to Play, Commands, How to Use Help, About |

The executable also contains the `DLG_GOTO`, `DLG_PASSWORD`, `DLG_BESTTIMES`,
and `DLG_COMPLETE` dialog resources at file offsets `0x3FE00` through
`0x40400`. These are required behaviors. The Vita interface may adapt their
input to controller navigation while preserving their text, choices, and
results.

## Reconstruction matrix

| System | State | Evidence or remaining requirement |
|---|---|---|
| DAT parsing | Confirmed | All 149 records and every metadata type present in the supplied DAT parse successfully. |
| Indexed palette | Confirmed | Exact 16 entry palettes come from `OBJ32_4`, `INFOWND`, `BACKGROUND`, `200`, and `CHIPEND`. |
| Board and information panel | Implemented | Counters follow `2:29A6`/`9:00EA`: leading zeros blank, yellow time at 15 seconds or less (yellow `---` when untimed), yellow chips at zero. |
| Board viewport | Implemented | Original 9 by 9 viewport and resources render in the Windows Vita3K build; physical Vita remains required. |
| Basic player interactions | Reconstructed | `7:1184` Chip movement, the tile rule table at `DS:066C` (`3:1A56`), pickups, doors, boots, thief, socket, hint, and exit follow the original. A recorded play-through from the Windows build is still needed as a regression corpus. |
| Clock | Reconstructed | Board timer 1 fires every 110 ms (`2:16FA`). A second is ten ticks of the never-reset counter `DS:064E` (`7:05BD`), and the timer is held after each load until the first key (`4:054D`). |
| Monsters | Reconstructed | `3:074E` per-type turning order, teeth and blob slow ticks, Microsoft C `rand()` (`1:00DC`), and the stale-direction quirk for teeth on traps. Needs trace confirmation. |
| Sliding and force floors | Reconstructed | Slip list processing (`3:13DE`), `7:0636` direction rules including ice corners and random force floors, and bounce handling follow the original. Needs trace confirmation. |
| Traps and clone machines | Reconstructed | Trap records keep the DAT fifth word as their held state; `3:21AA`, `3:211A`, and `3:2442` are reproduced. Needs trace confirmation. |
| Teleports | Reconstructed | `3:276A` reverse reading-order search with per-mover acceptance rules. Needs trace confirmation. |
| Progress, passwords, scores | Partial | Level Complete dialog and scoring (`6:0422`), attempt counting and the skip-level prompt (`4:0356`), and `ENTPACK.INI`-format progress (`2:198E`-`2:1C9F`) are reconstructed. Password entry, Go To, and Best Times dialogs remain absent. |
| Menus and messages | Partial | Death messages (`2:0B9A`), the trouble prompt, and Level Complete use Windows 3.1 style dialogs from the original templates. The hint window (`2:0C1A`, `2:2BBE`) and the level 50–140 interludes are reconstructed. Pause, menus, and the animated ending (`7:0A74`) are absent; the ending currently shows only its three messages. |
| Sound effects | Partial | The 15-entry sound table (`DS:0336`, `DS:040A`) and interrupting `sndPlaySound` semantics (`8:056C`) are reproduced. System sounds absent from the archive stay silent as with `SND_NODEFAULT`. Some engine triggers await the engine reconstruction. |
| Music | Missing | Both original MIDI resources are packaged but playback and the Background Music option are absent. |
| Controls | Partial | D-pad movement and Cross restart/proceed exist. Pause, menus, password entry, help, and controller repeat are absent. |
| ARM ABI release gate | Partial | VitaSDK produces ARMv7 code. Final dependency float ABI inspection and physical Vita/PSTV validation remain required. |

No release should be described as gold while any required row is Partial or
Missing.
