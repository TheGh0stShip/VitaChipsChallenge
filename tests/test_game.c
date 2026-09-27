#include "vcc/game.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); return 1; \
} } while (0)

static void blank_level(vcc_level *level)
{
    memset(level, 0, sizeof *level);
    level->number = 1U;
    level->time_limit = 100U;
    level->lower[1U * VCC_MAP_WIDTH + 1U] = VCC_CHIP_E;
}

int main(void)
{
    vcc_level level;
    vcc_game game;
    blank_level(&level);
    level.chips_required = 1U;
    level.lower[1U * VCC_MAP_WIDTH + 2U] = VCC_CHIP;
    level.lower[1U * VCC_MAP_WIDTH + 3U] = VCC_SOCKET;
    level.lower[1U * VCC_MAP_WIDTH + 4U] = VCC_EXIT;
    CHECK(vcc_game_start(&game, &level));
    CHECK(vcc_game_move(&game, VCC_DIR_EAST));
    CHECK(game.chips_left == 0U);
    CHECK(vcc_game_move(&game, VCC_DIR_EAST));
    CHECK(vcc_game_move(&game, VCC_DIR_EAST));
    CHECK(game.status == VCC_WON);

    blank_level(&level);
    level.lower[1U * VCC_MAP_WIDTH + 2U] = VCC_BLOCK;
    level.lower[1U * VCC_MAP_WIDTH + 3U] = VCC_WATER;
    CHECK(vcc_game_start(&game, &level));
    CHECK(vcc_game_move(&game, VCC_DIR_EAST));
    CHECK(game.terrain[1U * VCC_MAP_WIDTH + 3U] == VCC_DIRT);
    CHECK(game.actors[1U * VCC_MAP_WIDTH + 2U] == 0U);

    blank_level(&level);
    level.lower[1U * VCC_MAP_WIDTH + 2U] = VCC_FIRE;
    CHECK(vcc_game_start(&game, &level));
    CHECK(vcc_game_move(&game, VCC_DIR_EAST));
    CHECK(game.status == VCC_BURNED);
    CHECK(vcc_tile_sprite(VCC_EXIT) == VCC_EXIT);
    CHECK(vcc_tile_sprite(VCC_CHIP_S) == VCC_CHIP_S);
    puts("game core tests passed");
    return 0;
}
