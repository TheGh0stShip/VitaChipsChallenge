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

    puts("game core tests passed");
    return 0;
}
