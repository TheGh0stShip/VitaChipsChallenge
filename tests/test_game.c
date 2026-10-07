/* SPDX-License-Identifier: GPL-3.0-only */
#include "vcc/game.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); return 1; \
} } while (0)

#define CELL(x, y) ((size_t)(y) * VCC_MAP_WIDTH + (size_t)(x))

typedef struct recorder {
    int died;
    int reason;
    int completed;
    int sounds[VCC_SOUND_COUNT];
} recorder;

static void on_sound(void *context, vcc_sound_id id, int interrupt)
{
    (void)interrupt;
    ++((recorder *)context)->sounds[id];
}

/* Like 4:0356, a death reloads the level before the tick continues. */
static void on_died(void *context, vcc_game *game)
{
    ++((recorder *)context)->died;
    ((recorder *)context)->reason = game->death;
    (void)vcc_game_start(game, game->level);
}

static void on_completed(void *context, vcc_game *game)
{
    (void)game;
    ++((recorder *)context)->completed;
}

static vcc_level level;
static vcc_game game;
static recorder events;

static void blank(void)
{
    memset(&level, 0, sizeof level);
    level.number = 1U;
    level.time_limit = 100U;
    level.lower[CELL(1, 1)] = VCC_CHIP_E;
}

static void start(void)
{
    memset(&events, 0, sizeof events);
    memset(&game, 0, sizeof game);
    game.hooks.context = &events;
    game.hooks.sound = on_sound;
    game.hooks.died = on_died;
    game.hooks.completed = on_completed;
    (void)vcc_game_start(&game, &level);
}

/* Moves east once and advances a tick so the next key is not queued. */
static void step(vcc_direction direction)
{
    vcc_game_key(&game, direction);
    vcc_game_tick(&game);
}

int main(void)
{
    int i;

    /* Chips, socket, exit. */
    blank();
    level.chips_required = 1U;
    level.lower[CELL(2, 1)] = VCC_CHIP;
    level.lower[CELL(3, 1)] = VCC_SOCKET;
    level.lower[CELL(4, 1)] = VCC_EXIT;
    start();
    CHECK(game.chip_x == 1 && game.chip_y == 1 && game.waiting);
    step(VCC_DIR_EAST);
    CHECK(game.chips_left == 0 && events.sounds[VCC_SOUND_CHIP] == 1);
    step(VCC_DIR_EAST);
    CHECK(events.sounds[VCC_SOUND_SOCKET] == 1);
    step(VCC_DIR_EAST);
    CHECK(events.completed == 1 && events.sounds[VCC_SOUND_COMPLETE] == 1);

    /* The timer is held until the first key (4:054D). */
    blank();
    start();
    for (i = 0; i < 30; ++i) vcc_game_tick(&game);
    CHECK(game.ticks == 0 && game.time_left == 100);

    /* A second key in the same tick is queued, not lost (7:11C7). */
    blank();
    start();
    vcc_game_key(&game, VCC_DIR_EAST);
    vcc_game_key(&game, VCC_DIR_EAST);
    CHECK(game.chip_x == 2 && game.pending);
    vcc_game_tick(&game);
    CHECK(game.chip_x == 2 && game.pending);
    vcc_game_tick(&game);
    CHECK(game.chip_x == 3 && !game.pending);

    /* One second per ten ticks of the global counter (7:05BD). */
    blank();
    start();
    vcc_game_key(&game, VCC_DIR_SOUTH);
    for (i = 0; i < 10; ++i) vcc_game_tick(&game);
    CHECK(game.time_left == 99);

    /* A block pushed into water becomes dirt with a splash. */
    blank();
    level.lower[CELL(2, 1)] = VCC_BLOCK;
    level.lower[CELL(3, 1)] = VCC_WATER;
    start();
    step(VCC_DIR_EAST);
    CHECK(game.top[CELL(3, 1)] == VCC_DIRT && game.chip_x == 2);
    CHECK(events.sounds[VCC_SOUND_SPLASH] == 1);

    /* Fire without boots kills Chip and shows the burned tile. */
    blank();
    level.lower[CELL(2, 1)] = VCC_FIRE;
    start();
    vcc_game_key(&game, VCC_DIR_EAST);
    CHECK(events.died == 1 && events.reason == VCC_DEATH_FIRE);
    CHECK(game.chip_x == 1 && game.waiting);

    /* Walls refuse with BlockedMoveSound and Chip turns to face them. */
    blank();
    level.lower[CELL(2, 1)] = VCC_WALL;
    start();
    vcc_game_key(&game, VCC_DIR_EAST);
    CHECK(game.chip_x == 1 && events.sounds[VCC_SOUND_BLOCKED] == 1);

    /* Thin wall on the east edge stops leaving eastward (3:1934). */
    blank();
    level.lower[CELL(1, 1)] = VCC_THIN_EAST;
    level.upper[CELL(1, 1)] = VCC_FLOOR;
    level.lower[CELL(1, 2)] = VCC_CHIP_N;
    start();
    step(VCC_DIR_NORTH);
    CHECK(game.chip_y == 1);
    step(VCC_DIR_EAST);
    CHECK(game.chip_x == 1);

    /* A ball bounces between walls and kills Chip on contact. */
    blank();
    level.lower[CELL(1, 1)] = VCC_CHIP_S;
    level.lower[CELL(5, 1)] = VCC_BALL_N + 3;
    level.lower[CELL(6, 1)] = VCC_WALL;
    level.creature_count = 1U;
    level.creatures[0].x = 5;
    level.creatures[0].y = 1;
    start();
    CHECK(game.monster_count == 1U);
    vcc_game_key(&game, VCC_DIR_NONE);
    for (i = 0; i < 12 && !events.died; ++i) vcc_game_tick(&game);
    CHECK(events.died == 1 && events.reason == VCC_DEATH_MONSTER);

    /* Ice keeps Chip sliding until the far side. */
    blank();
    level.lower[CELL(2, 1)] = VCC_ICE;
    level.lower[CELL(3, 1)] = VCC_ICE;
    start();
    vcc_game_key(&game, VCC_DIR_EAST);
    CHECK(game.sliding);
    for (i = 0; i < 4; ++i) vcc_game_tick(&game);
    CHECK(game.chip_x == 4 && !game.sliding);

    /* Keys are spent at doors except green, which 3:1804 never decrements. */
    blank();
    level.lower[CELL(2, 1)] = VCC_GREEN_KEY;
    level.lower[CELL(3, 1)] = VCC_GREEN_DOOR;
    level.lower[CELL(4, 1)] = VCC_GREEN_DOOR;
    level.lower[CELL(5, 1)] = VCC_BLUE_KEY;
    level.lower[CELL(6, 1)] = VCC_BLUE_DOOR;
    level.lower[CELL(7, 1)] = VCC_BLUE_DOOR;
    start();
    step(VCC_DIR_EAST);
    CHECK(game.keys[2] == 1 && events.sounds[VCC_SOUND_TOOL] == 1);
    step(VCC_DIR_EAST);
    step(VCC_DIR_EAST);
    CHECK(game.chip_x == 4 && game.keys[2] == 1 && events.sounds[VCC_SOUND_DOOR] == 2);
    step(VCC_DIR_EAST);
    step(VCC_DIR_EAST);
    CHECK(game.chip_x == 6 && game.keys[0] == 0);
    step(VCC_DIR_EAST);
    CHECK(game.chip_x == 6 && events.sounds[VCC_SOUND_BLOCKED] == 1);

    /* Flippers let Chip swim (3:187C turns the die action into enter). */
    blank();
    level.lower[CELL(2, 1)] = VCC_FLIPPERS;
    level.lower[CELL(3, 1)] = VCC_WATER;
    start();
    step(VCC_DIR_EAST);
    step(VCC_DIR_EAST);
    CHECK(events.died == 0 && game.chip_x == 3 && game.boots[0] == 1);
    CHECK(game.bottom[CELL(3, 1)] == VCC_WATER);
    CHECK(game.top[CELL(3, 1)] == VCC_CHIP_SWIM_N + 3);

    /* Fire boots let Chip stand in fire. */
    blank();
    level.lower[CELL(2, 1)] = VCC_FIRE_BOOTS;
    level.lower[CELL(3, 1)] = VCC_FIRE;
    start();
    step(VCC_DIR_EAST);
    step(VCC_DIR_EAST);
    CHECK(events.died == 0 && game.chip_x == 3 && game.boots[1] == 1);
    CHECK(game.bottom[CELL(3, 1)] == VCC_FIRE && game.top[CELL(3, 1)] == VCC_CHIP_E);

    /* The thief takes boots only (3:1734 called with boots_only set). */
    blank();
    level.lower[CELL(2, 1)] = VCC_FLIPPERS;
    level.lower[CELL(3, 1)] = VCC_BLUE_KEY;
    level.lower[CELL(4, 1)] = VCC_THIEF;
    start();
    for (i = 0; i < 3; ++i) step(VCC_DIR_EAST);
    CHECK(game.chip_x == 4 && events.sounds[VCC_SOUND_THIEF] == 1);
    CHECK(game.boots[0] == 0 && game.keys[0] == 1);

    /* The green button flips every toggle tile, silently (3:1FAC). */
    blank();
    level.lower[CELL(2, 1)] = VCC_GREEN_BUTTON;
    level.lower[CELL(3, 2)] = VCC_TOGGLE_WALL;
    level.lower[CELL(4, 2)] = VCC_TOGGLE_FLOOR;
    start();
    CHECK(game.toggle_count == 2U);
    step(VCC_DIR_EAST);
    CHECK(game.top[CELL(3, 2)] == VCC_TOGGLE_FLOOR && game.top[CELL(4, 2)] == VCC_TOGGLE_WALL);
    CHECK(events.sounds[VCC_SOUND_SWITCH] == 0);

    /* A trap holds Chip until a ball rolls onto its brown button (3:21AA,
     * 3:211A). */
    blank();
    level.lower[CELL(2, 1)] = VCC_TRAP;
    level.lower[CELL(4, 3)] = VCC_BROWN_BUTTON;
    level.lower[CELL(5, 3)] = VCC_BALL_N + 1;
    level.lower[CELL(2, 3)] = VCC_WALL;
    level.creature_count = 1U;
    level.creatures[0].x = 5;
    level.creatures[0].y = 3;
    level.trap_count = 1U;
    level.traps[0].button_x = 4;
    level.traps[0].button_y = 3;
    level.traps[0].trap_x = 2;
    level.traps[0].trap_y = 1;
    start();
    step(VCC_DIR_EAST);
    CHECK(game.chip_x == 2 && game.traps[0].closed == 1);
    step(VCC_DIR_EAST); /* refused; the even tick moves the ball */
    CHECK(game.chip_x == 2 && events.sounds[VCC_SOUND_BLOCKED] == 1);
    CHECK(game.monsters[0].x == 4 && game.traps[0].closed == 0);
    step(VCC_DIR_EAST);
    CHECK(game.chip_x == 3 && events.died == 0);

    /* The blue button turns tanks around (3:1E6A). */
    blank();
    level.lower[CELL(2, 1)] = VCC_BLUE_BUTTON;
    level.lower[CELL(6, 6)] = VCC_TANK_N;
    level.creature_count = 1U;
    level.creatures[0].x = 6;
    level.creatures[0].y = 6;
    start();
    step(VCC_DIR_EAST); /* odd tick: monsters stay put */
    CHECK(events.sounds[VCC_SOUND_SWITCH] == 1);
    CHECK(game.monsters[0].dx == 0 && game.monsters[0].dy == 1);
    CHECK(game.top[CELL(6, 6)] == VCC_TANK_N + 2);

    /* Teleports search backwards in reading order, skipping one whose exit
     * is walled (3:276A), then Chip slides out. */
    blank();
    level.lower[CELL(2, 1)] = VCC_TELEPORT;
    level.lower[CELL(2, 4)] = VCC_TELEPORT;
    level.lower[CELL(2, 6)] = VCC_TELEPORT;
    level.lower[CELL(3, 6)] = VCC_WALL;
    start();
    CHECK(game.teleport_count == 3U);
    vcc_game_key(&game, VCC_DIR_EAST);
    CHECK(game.chip_x == 2 && game.chip_y == 4 && game.sliding);
    CHECK(events.sounds[VCC_SOUND_TELEPORT] == 1);
    vcc_game_tick(&game);
    CHECK(game.chip_x == 3 && game.chip_y == 4 && !game.sliding);

    /* A force floor redirects Chip whatever direction he entered it. */
    blank();
    level.lower[CELL(2, 1)] = VCC_FORCE_SOUTH;
    level.lower[CELL(2, 2)] = VCC_FORCE_SOUTH;
    start();
    vcc_game_key(&game, VCC_DIR_EAST);
    CHECK(game.sliding && game.slide_dx == 0 && game.slide_dy == 1);
    for (i = 0; i < 4; ++i) vcc_game_tick(&game);
    CHECK(game.chip_x == 2 && game.chip_y == 3 && !game.sliding);

    /* A block pushed onto a bomb blows both up, leaving floor (7:0DAE). */
    blank();
    level.lower[CELL(2, 1)] = VCC_BLOCK;
    level.lower[CELL(3, 1)] = VCC_BOMB;
    start();
    step(VCC_DIR_EAST);
    CHECK(game.chip_x == 2 && game.top[CELL(3, 1)] == VCC_FLOOR);
    CHECK(events.sounds[VCC_SOUND_BOMB] == 1 && events.died == 0);

    /* A popup wall rises behind Chip. */
    blank();
    level.lower[CELL(2, 1)] = VCC_POPUP_WALL;
    start();
    step(VCC_DIR_EAST);
    CHECK(game.chip_x == 2);
    step(VCC_DIR_EAST);
    CHECK(game.chip_x == 3 && game.top[CELL(2, 1)] == VCC_WALL);
    step(VCC_DIR_WEST);
    CHECK(game.chip_x == 3);

    /* A fake blue wall vanishes; a real one turns into plain wall. */
    blank();
    level.lower[CELL(2, 1)] = VCC_BLUE_WALL_FAKE;
    level.lower[CELL(1, 2)] = VCC_BLUE_WALL_REAL;
    start();
    step(VCC_DIR_SOUTH);
    CHECK(game.chip_y == 1 && game.top[CELL(1, 2)] == VCC_WALL);
    step(VCC_DIR_EAST);
    CHECK(game.chip_x == 2);
    step(VCC_DIR_WEST);
    CHECK(game.chip_x == 1 && game.top[CELL(2, 1)] == VCC_FLOOR);

    /* The hint tile is entered like floor and stays underneath Chip. */
    blank();
    level.lower[CELL(2, 1)] = VCC_HINT;
    start();
    step(VCC_DIR_EAST);
    CHECK(game.chip_x == 2 && vcc_game_terrain_tile(&game, 2, 1) == VCC_HINT);
    step(VCC_DIR_EAST);
    CHECK(game.top[CELL(2, 1)] == VCC_HINT);

    puts("game core tests passed");
    return 0;
}
