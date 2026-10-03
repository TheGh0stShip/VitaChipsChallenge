/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VCC_GAME_H
#define VCC_GAME_H

#include "vcc/dat.h"

#include <stdint.h>

/* Game engine reconstructed from CHIPS.EXE code segments 3 and 7. Field
 * comments give the offset in the Win16 game state block (DS:1680) or the
 * DS global they mirror. Routine addresses are recorded in src/game.c. */

typedef enum vcc_direction {
    VCC_DIR_NORTH = 0,
    VCC_DIR_WEST = 1,
    VCC_DIR_SOUTH = 2,
    VCC_DIR_EAST = 3,
    VCC_DIR_NONE = 4
} vcc_direction;

/* Death reasons, state+0x816, indexing the messages at 2:0BC8. */
typedef enum vcc_death {
    VCC_ALIVE = 0,
    VCC_DEATH_FIRE = 1,
    VCC_DEATH_WATER = 2,
    VCC_DEATH_BOMB = 3,
    VCC_DEATH_BLOCK = 4,
    VCC_DEATH_MONSTER = 5,
    VCC_DEATH_TIME = 6
} vcc_death;

/* Sound indices played through 8:056C; names and default files are listed
 * at DS:0336 and DS:040A. */
typedef enum vcc_sound_id {
    VCC_SOUND_TOOL = 0,        /* PickUpToolSound        blip2.wav */
    VCC_SOUND_DOOR = 1,        /* OpenDoorSound          door.wav */
    VCC_SOUND_DEATH = 2,       /* ChipDeathSound         bummer.wav */
    VCC_SOUND_COMPLETE = 3,    /* LevelCompleteSound     ditty1.wav */
    VCC_SOUND_SOCKET = 4,      /* SocketSound            chimes.wav */
    VCC_SOUND_BLOCKED = 5,     /* BlockedMoveSound       oof3.wav */
    VCC_SOUND_THIEF = 6,       /* ThiefSound             strike.wav */
    VCC_SOUND_SOUND_ON = 7,    /* SoundOnSound           chimes.wav */
    VCC_SOUND_CHIP = 8,        /* PickUpChipSound        click3.wav */
    VCC_SOUND_SWITCH = 9,      /* SwitchSound            pop2.wav */
    VCC_SOUND_SPLASH = 10,     /* SplashSound            water2.wav */
    VCC_SOUND_BOMB = 11,       /* BombSound              hit3.wav */
    VCC_SOUND_TELEPORT = 12,   /* TeleportSound          teleport.wav */
    VCC_SOUND_TICK = 13,       /* TickSound              click1.wav */
    VCC_SOUND_DEATH_TIME = 14, /* ChipDeathByTimeSound   bell.wav */
    VCC_SOUND_COUNT = 15
} vcc_sound_id;

/* Tile codes as stored in the DAT layers and the Win16 map. */
enum vcc_tile {
    VCC_FLOOR = 0x00,
    VCC_WALL = 0x01,
    VCC_CHIP = 0x02,
    VCC_WATER = 0x03,
    VCC_FIRE = 0x04,
    VCC_INVISIBLE_WALL = 0x05,
    VCC_THIN_NORTH = 0x06,
    VCC_THIN_WEST = 0x07,
    VCC_THIN_SOUTH = 0x08,
    VCC_THIN_EAST = 0x09,
    VCC_BLOCK = 0x0A,
    VCC_DIRT = 0x0B,
    VCC_ICE = 0x0C,
    VCC_FORCE_SOUTH = 0x0D,
    VCC_CLONE_BLOCK_N = 0x0E,
    VCC_CLONE_BLOCK_W = 0x0F,
    VCC_CLONE_BLOCK_S = 0x10,
    VCC_CLONE_BLOCK_E = 0x11,
    VCC_FORCE_NORTH = 0x12,
    VCC_FORCE_EAST = 0x13,
    VCC_FORCE_WEST = 0x14,
    VCC_EXIT = 0x15,
    VCC_BLUE_DOOR = 0x16,
    VCC_RED_DOOR = 0x17,
    VCC_GREEN_DOOR = 0x18,
    VCC_YELLOW_DOOR = 0x19,
    VCC_ICE_NW = 0x1A,
    VCC_ICE_NE = 0x1B,
    VCC_ICE_SE = 0x1C,
    VCC_ICE_SW = 0x1D,
    VCC_BLUE_WALL_FAKE = 0x1E,
    VCC_BLUE_WALL_REAL = 0x1F,
    VCC_THIEF = 0x21,
    VCC_SOCKET = 0x22,
    VCC_GREEN_BUTTON = 0x23,
    VCC_RED_BUTTON = 0x24,
    VCC_TOGGLE_WALL = 0x25,
    VCC_TOGGLE_FLOOR = 0x26,
    VCC_BROWN_BUTTON = 0x27,
    VCC_BLUE_BUTTON = 0x28,
    VCC_TELEPORT = 0x29,
    VCC_BOMB = 0x2A,
    VCC_TRAP = 0x2B,
    VCC_HIDDEN_WALL = 0x2C,
    VCC_GRAVEL = 0x2D,
    VCC_POPUP_WALL = 0x2E,
    VCC_HINT = 0x2F,
    VCC_THIN_SE = 0x30,
    VCC_CLONE_MACHINE = 0x31,
    VCC_RANDOM_FORCE = 0x32,
    VCC_CHIP_DROWNED = 0x33,
    VCC_CHIP_BURNED = 0x34,
    VCC_CHIP_BOMBED = 0x35,
    VCC_EXIT_ANIM_1 = 0x3A,
    VCC_EXIT_ANIM_2 = 0x3B,
    VCC_CHIP_SWIM_N = 0x3C,
    VCC_CHIP_SWIM_S = 0x3E,
    VCC_BUG_N = 0x40,
    VCC_FIREBALL_N = 0x44,
    VCC_BALL_N = 0x48,
    VCC_TANK_N = 0x4C,
    VCC_GLIDER_N = 0x50,
    VCC_TEETH_N = 0x54,
    VCC_WALKER_N = 0x58,
    VCC_BLOB_N = 0x5C,
    VCC_PARAMECIUM_N = 0x60,
    VCC_BLUE_KEY = 0x64,
    VCC_RED_KEY = 0x65,
    VCC_GREEN_KEY = 0x66,
    VCC_YELLOW_KEY = 0x67,
    VCC_FLIPPERS = 0x68,
    VCC_FIRE_BOOTS = 0x69,
    VCC_ICE_SKATES = 0x6A,
    VCC_SUCTION_BOOTS = 0x6B,
    VCC_CHIP_N = 0x6C,
    VCC_CHIP_W = 0x6D,
    VCC_CHIP_S = 0x6E,
    VCC_CHIP_E = 0x6F
};

#define VCC_MAX_MOVERS 1024U
#define VCC_MAX_TRAPS 64U
#define VCC_MAX_CLONES 64U

/* Monster and slip list record, 11 bytes in the original. */
typedef struct vcc_mover {
    uint8_t tile;
    int16_t x;
    int16_t y;
    int16_t dx;
    int16_t dy;
    int16_t flag;  /* monster: is sliding; slip entry: is a monster */
} vcc_mover;

typedef struct vcc_trap {
    int16_t button_x;
    int16_t button_y;
    int16_t trap_x;
    int16_t trap_y;
    int16_t closed;
} vcc_trap;

typedef struct vcc_clone {
    int16_t button_x;
    int16_t button_y;
    int16_t machine_x;
    int16_t machine_y;
} vcc_clone;

typedef struct vcc_point {
    int16_t x;
    int16_t y;
} vcc_point;

struct vcc_game;

/* Calls the Win16 game made into its user interface from inside a timer
 * tick or key press. The dialogs these represent are modal, so the hooks
 * run them to completion before returning, exactly as DialogBox did. */
typedef struct vcc_game_hooks {
    void *context;
    /* sndPlaySound; `interrupt` is false for SND_NOSTOP calls. */
    void (*sound)(void *context, vcc_sound_id sound, int interrupt);
    /* 2:0B9A then 4:0356(level, retry): show the death message and reload. */
    void (*died)(void *context, struct vcc_game *game);
    /* 7:0CCA: Level Complete dialog, interludes, and the next level. */
    void (*completed)(void *context, struct vcc_game *game);
} vcc_game_hooks;

typedef struct vcc_game {
    const vcc_level *level;
    vcc_game_hooks hooks;

    uint8_t top[VCC_MAP_CELLS];       /* state+0x000 */
    uint8_t bottom[VCC_MAP_CELLS];    /* state+0x400 */
    int16_t chip_x;                   /* state+0x808 */
    int16_t chip_y;                   /* state+0x80A */
    int16_t sliding;                  /* state+0x80C */
    int16_t pending;                  /* state+0x80E queued key move */
    int16_t waiting;                  /* state+0x810 timer held until input */
    int16_t pending_dx;               /* state+0x812 */
    int16_t pending_dy;               /* state+0x814 */
    int16_t death;                    /* state+0x816 */
    int16_t slide_dx;                 /* state+0x818 */
    int16_t slide_dy;                 /* state+0x81A */

    uint16_t slip_count;              /* state+0x91E */
    vcc_mover slips[VCC_MAX_MOVERS];  /* state+0x924 */
    uint16_t monster_count;           /* state+0x928 */
    vcc_mover monsters[VCC_MAX_MOVERS]; /* state+0x92E */
    uint16_t toggle_count;            /* state+0x932 */
    vcc_point toggles[VCC_MAX_MOVERS];  /* state+0x938 */
    uint16_t trap_count;              /* state+0x93C */
    vcc_trap traps[VCC_MAX_TRAPS];    /* state+0x942 */
    uint16_t clone_count;             /* state+0x946 */
    vcc_clone clones[VCC_MAX_CLONES]; /* state+0x94C */
    uint16_t teleport_count;          /* state+0x950 */
    vcc_point teleports[VCC_MAX_MOVERS]; /* state+0x956 */

    int16_t moves;                    /* state+0xA34 */
    int16_t mouse_active;             /* state+0xA38 */
    int16_t mouse_x;                  /* state+0xA3A */
    int16_t mouse_y;                  /* state+0xA3C */
    int16_t idle;                     /* state+0xA3E */
    int16_t moved;                    /* state+0xA40 Chip moved this tick */

    int16_t time_left;                /* DS:1694 seconds */
    int16_t chips_left;               /* DS:1692 */
    int16_t keys[4];                  /* DS:1682 blue, red, green, yellow */
    int16_t boots[4];                 /* DS:168A flippers, fire, ice, suction */
    int16_t paused;                   /* DS:0022 Game > Pause */

    /* Process-wide state the loader does not reset. */
    uint16_t ticks;                   /* DS:064E board timer count */
    uint32_t random_seed;             /* DS:1498 C runtime rand() */
} vcc_game;

/* Seeds rand() as WinMain does with GetCurrentTime (2:092E). */
void vcc_game_seed(vcc_game *game, uint32_t seed);
/* Loads a level (4:0356 after its prompts). Preserves ticks and the seed. */
int vcc_game_start(vcc_game *game, const vcc_level *level);
/* WM_KEYDOWN (2:2540): any key releases the waiting timer; arrows move. */
void vcc_game_key(vcc_game *game, vcc_direction direction);
/* Board WM_LBUTTONDOWN (2:27EA): walk toward a map cell. */
void vcc_game_click(vcc_game *game, int x, int y);
/* Board WM_TIMER, timer 1 every 110 ms (2:27B0). */
void vcc_game_tick(vcc_game *game);

uint8_t vcc_game_terrain_tile(const vcc_game *game, uint8_t x, uint8_t y);
uint8_t vcc_game_actor_tile(const vcc_game *game, uint8_t x, uint8_t y);
uint8_t vcc_game_tile(const vcc_game *game, uint8_t x, uint8_t y, int bottom);
unsigned vcc_tile_sprite(unsigned tile);

#endif
