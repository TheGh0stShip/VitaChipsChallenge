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
    VCC_COLLIDED,
    VCC_TIMEOUT
} vcc_status;

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
    uint16_t chips_left;
    uint32_t ticks;
    uint32_t time_left_ticks;
    vcc_status status;
} vcc_game;

int vcc_game_start(vcc_game *game, const vcc_level *level);
int vcc_game_move(vcc_game *game, vcc_direction direction);
void vcc_game_tick(vcc_game *game, vcc_direction input);
uint8_t vcc_game_visible_tile(const vcc_game *game, uint8_t x, uint8_t y);
unsigned vcc_tile_sprite(unsigned tile);

#endif

