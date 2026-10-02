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
| Board and information panel | Implemented | Original 9 by 9 viewport and resources render in the Windows Vita3K build; physical Vita remains required. |
| Basic player interactions | Implemented | Floor, walls, chips, sockets, keys, doors, boots, thief, hazards, exit, dirt, and blocks exist. Reference traces remain incomplete. |
| Clock | Implemented | Starts after Chip first moves, advances at 20 engine ticks per displayed count, changes to yellow and clicks at 15. Exact Win16 timer drift is not reproduced yet. |
| Monsters | Partial | Nine families exist, but MS movement phases, collision order, slide delay, random behavior, and known MS edge cases remain unconfirmed. |
| Sliding and force floors | Partial | Player movement exists. Block and monster sliding, boosting, spring steps, and slide delay remain incomplete. |
| Traps and clone machines | Partial | DAT links and basic activation exist. Persistence of MS trap release and all movement ordering need reference traces. |
| Teleports | Partial | Basic player search exists. Blocks, monsters, stuck exits, collision order, and teleport network edge cases remain incomplete. |
| Progress, passwords, scores | Missing | Passwords parse, but dialogs, attempts, score integration, best times, save format, and level unlocking are absent. |
| Menus and messages | Missing | Death, hint, completion, ending, menu, and dialog flows are absent. |
| Sound effects | Implemented | Original WAV resources are packaged and mapped to an initial event set; exact trigger priority remains unconfirmed. |
| Music | Missing | Both original MIDI resources are packaged but playback and the Background Music option are absent. |
| Controls | Partial | D-pad movement and Cross restart/proceed exist. Pause, menus, password entry, help, and controller repeat are absent. |
| ARM ABI release gate | Partial | VitaSDK produces ARMv7 code. Final dependency float ABI inspection and physical Vita/PSTV validation remain required. |

No release should be described as gold while any required row is Partial or
Missing.
