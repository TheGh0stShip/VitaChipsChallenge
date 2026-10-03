/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VCC_GAME_H
#define VCC_GAME_H

#include "vcc/dat.h"

#include <stdint.h>

typedef enum vcc_direction {
    VCC_DIR_NORTH = 0,
    VCC_DIR_WEST = 1,
    VCC_DIR_SOUTH = 2,
    VCC_DIR_EAST = 3,
    VCC_DIR_NONE = 4
} vcc_direction;

typedef enum vcc_status {
    VCC_PLAYING = 0,
    VCC_WON,
    VCC_DROWNED,
    VCC_BURNED,
    VCC_BOMBED,
    VCC_SQUASHED,
    VCC_COLLIDED,
    VCC_TIMEOUT
} vcc_status;

/* Sound events. Value - 1 is the original sound index used by the Win16
 * player at 8:056C, whose names and default files are tabulated at
 * DS:0336 and DS:040A. */
typedef enum vcc_event {
    VCC_EVENT_NONE = 0,
    VCC_EVENT_TOOL,          /* 0  PickUpToolSound        blip2.wav */
    VCC_EVENT_DOOR,          /* 1  OpenDoorSound          door.wav */
    VCC_EVENT_DEATH,         /* 2  ChipDeathSound         bummer.wav */
    VCC_EVENT_COMPLETE,      /* 3  LevelCompleteSound     ditty1.wav */
    VCC_EVENT_SOCKET,        /* 4  SocketSound            chimes.wav */
    VCC_EVENT_BLOCKED,       /* 5  BlockedMoveSound       oof3.wav */
    VCC_EVENT_THIEF,         /* 6  ThiefSound             strike.wav */
    VCC_EVENT_SOUND_ON,      /* 7  SoundOnSound           chimes.wav */
    VCC_EVENT_CHIP,          /* 8  PickUpChipSound        click3.wav */
    VCC_EVENT_BUTTON,        /* 9  SwitchSound            pop2.wav */
    VCC_EVENT_SPLASH,        /* 10 SplashSound            water2.wav */
    VCC_EVENT_BOMB,          /* 11 BombSound              hit3.wav */
    VCC_EVENT_TELEPORT,      /* 12 TeleportSound          teleport.wav */
    VCC_EVENT_TICK,          /* 13 TickSound              click1.wav */
    VCC_EVENT_DEATH_TIME,    /* 14 ChipDeathByTimeSound   bell.wav */
    VCC_EVENT_COUNT
} vcc_event;

enum vcc_tile {
    VCC_FLOOR = 0,
    VCC_WALL = 1,
    VCC_CHIP = 2,
    VCC_WATER = 3,
    VCC_FIRE = 4,
    VCC_INVISIBLE_WALL = 5,
    VCC_THIN_NORTH = 6,
    VCC_THIN_WEST = 7,
    VCC_THIN_SOUTH = 8,
    VCC_THIN_EAST = 9,
    VCC_BLOCK = 10,
    VCC_DIRT = 11,
    VCC_ICE = 12,
    VCC_FORCE_SOUTH = 13,
    VCC_CLONE_BLOCK_N = 14,
    VCC_CLONE_BLOCK_W = 15,
    VCC_CLONE_BLOCK_S = 16,
    VCC_CLONE_BLOCK_E = 17,
    VCC_FORCE_NORTH = 18,
    VCC_FORCE_EAST = 19,
    VCC_FORCE_WEST = 20,
    VCC_EXIT = 21,
    VCC_BLUE_DOOR = 22,
    VCC_RED_DOOR = 23,
    VCC_GREEN_DOOR = 24,
    VCC_YELLOW_DOOR = 25,
    VCC_ICE_NW = 26,
    VCC_ICE_NE = 27,
    VCC_ICE_SE = 28,
    VCC_ICE_SW = 29,
    VCC_BLUE_WALL_FAKE = 30,
    VCC_BLUE_WALL_REAL = 31,
    VCC_THIEF = 33,
    VCC_SOCKET = 34,
    VCC_GREEN_BUTTON = 35,
    VCC_RED_BUTTON = 36,
    VCC_TOGGLE_WALL = 37,
    VCC_TOGGLE_FLOOR = 38,
    VCC_BROWN_BUTTON = 39,
    VCC_BLUE_BUTTON = 40,
    VCC_TELEPORT = 41,
    VCC_BOMB = 42,
    VCC_TRAP = 43,
    VCC_HIDDEN_WALL = 44,
    VCC_GRAVEL = 45,
    VCC_POPUP_WALL = 46,
    VCC_HINT = 47,
    VCC_THIN_SE = 48,
    VCC_CLONE_MACHINE = 49,
    VCC_RANDOM_FORCE = 50,
    VCC_BUG_N = 64,
    VCC_FIREBALL_N = 68,
    VCC_BALL_N = 72,
    VCC_TANK_N = 76,
    VCC_GLIDER_N = 80,
    VCC_TEETH_N = 84,
    VCC_WALKER_N = 88,
    VCC_BLOB_N = 92,
    VCC_PARAMECIUM_N = 96,
    VCC_BLUE_KEY = 100,
    VCC_RED_KEY = 101,
    VCC_GREEN_KEY = 102,
    VCC_YELLOW_KEY = 103,
    VCC_FLIPPERS = 104,
    VCC_FIRE_BOOTS = 105,
    VCC_ICE_SKATES = 106,
    VCC_SUCTION_BOOTS = 107,
    VCC_CHIP_N = 108,
    VCC_CHIP_W = 109,
    VCC_CHIP_S = 110,
    VCC_CHIP_E = 111
};

typedef struct vcc_game {
    const vcc_level *level;
    uint8_t terrain[VCC_MAP_CELLS];
    uint8_t actors[VCC_MAP_CELLS];
    uint8_t player_x;
    uint8_t player_y;
    uint8_t player_direction;
    uint8_t keys[4];
    uint8_t boots[4];
    vcc_position creature_order[VCC_MAX_CREATURES];
    uint8_t creature_count;
    uint32_t random_state;
    vcc_event last_event;
    uint16_t chips_left;
    uint32_t ticks;
    uint8_t timer_started;
    uint32_t time_left_ticks;
    uint16_t moves;  /* successful Chip steps, state+0xA34 (7:180F) */
    vcc_status status;
} vcc_game;

int vcc_game_start(vcc_game *game, const vcc_level *level);
int vcc_game_move(vcc_game *game, vcc_direction direction);
void vcc_game_tick(vcc_game *game, vcc_direction input);
uint8_t vcc_game_visible_tile(const vcc_game *game, uint8_t x, uint8_t y);
uint8_t vcc_game_terrain_tile(const vcc_game *game, uint8_t x, uint8_t y);
uint8_t vcc_game_actor_tile(const vcc_game *game, uint8_t x, uint8_t y);
vcc_event vcc_game_take_event(vcc_game *game);
unsigned vcc_tile_sprite(unsigned tile);

#endif
