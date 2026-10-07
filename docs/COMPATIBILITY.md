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
| Board viewport | Implemented | Original 9 by 9 viewport and resources render in Vita3K and on PS Vita hardware. |
| Basic player interactions | Reconstructed | `7:1184` Chip movement, the tile rule table at `DS:066C` (`3:1A56`), pickups, doors, boots, thief, socket, hint, and exit follow the original. A recorded play-through from the Windows build is still needed as a regression corpus. |
| Clock | Reconstructed | Board timer 1 fires every 110 ms (`2:16FA`). A second is ten ticks of the never-reset counter `DS:064E` (`7:05BD`), and the timer is held after each load until the first key (`4:054D`). |
| Monsters | Reconstructed | `3:074E` per-type turning order, teeth and blob slow ticks, Microsoft C `rand()` (`1:00DC`), and the stale-direction quirk for teeth on traps. Needs trace confirmation. |
| Sliding and force floors | Reconstructed | Slip list processing (`3:13DE`), `7:0636` direction rules including ice corners and random force floors, and bounce handling follow the original. Needs trace confirmation. |
| Traps and clone machines | Reconstructed | Trap records keep the DAT fifth word as their held state; `3:21AA`, `3:211A`, and `3:2442` are reproduced. Needs trace confirmation. |
| Teleports | Reconstructed | `3:276A` reverse reading-order search with per-mover acceptance rules. Needs trace confirmation. |
| Progress, passwords, scores | Reconstructed | Level Complete scoring (`6:0422`), attempts and the skip prompt (`4:0356`), `ENTPACK.INI` progress and options (`2:198E`-`2:1C9F`, `2:18DE`), the password gate (`4:115C`, `4:0E48`, DLG_PASSWORD `4:1016`), Go To (`6:0000`, `4:0EAA`), Best Times (`6:018E`), and New Game (`2:1DAE`). |
| Menus and messages | Reconstructed | CHIPSMENU with its accelerators and every command (`2:1E28`); death, trouble, completion, interlude, and ending messages; the level title and password overlay (`2:1374`); the paused board (`2:10DE`); the hint window; and WEP4UTIL's About dialog. Win3.1 menus and dialogs are redrawn, not the native ones. |
| Sound effects | Reconstructed | The 15-entry sound table (`DS:0336`, `DS:040A`) and interrupting `sndPlaySound` semantics (`8:056C`) are reproduced, and the engine raises each sound where the original does. System sounds absent from the archive stay silent as with `SND_NODEFAULT`. |
| Music | Implemented | The level's song (level number modulo the songs present, from the list at `DS:04A6`, `8:0308`) loops and follows Pause and Options. Playback uses a built-in OPL2-model FM synthesizer with Freedoom's GENMIDI bank, standing in for the Windows MIDI Mapper and FM driver; exact timbre depends on the original sound hardware. |
| Controls | Implemented | Arrows with Windows-style repeat, mouse or touch walking (`2:27EA`), menus (Start), Pause (Select), Restart (Triangle), Previous and Next (L and R), dialog navigation, and the Vita IME for text fields. |
| ARM ABI release gate | Partial | VitaSDK produces ARMv7 code. Builds run on PS Vita hardware. Float ABI inspection of all linked dependencies and PSTV validation remain required. |

No release should be described as gold while any required row is Partial or
Missing.

## Help

`tools/convert_help.py` converts the supplied WinHelp 3.0 `CHIPS.HLP`
(internal file system, `|Phrases`, `|TOPIC`, `|FONT`, `|TOMAP`, keyword
B-tree, and RLE pictures) at build time. The viewer shows its topics with
the original fonts, pictures, jumps, and popups, opened by the HELP_KEY
keywords the game passes to WEPHELP. Help on Help needs Windows'
`WINHELP.HLP`, which is not part of the archive, so it opens the contents.

## Defaults

Like the original (`2:18DE`), a fresh profile starts with Sound Effects and
Background Music off. Turn them on from the Options menu.
