#include "vcc/game.h"

#include <string.h>

static const int8_t direction_x[4] = {0, -1, 0, 1};
static const int8_t direction_y[4] = {-1, 0, 1, 0};

static int is_creature(uint8_t tile)
{
    return tile >= VCC_BUG_N && tile < VCC_BLUE_KEY;
}

static int is_player(uint8_t tile)
{
    return tile >= VCC_CHIP_N && tile <= VCC_CHIP_E;
}

static int is_actor(uint8_t tile)
{
    return tile == VCC_BLOCK || is_creature(tile) || is_player(tile);
}

static size_t cell(unsigned x, unsigned y)
{
    return (size_t)y * VCC_MAP_WIDTH + x;
}

static int in_bounds(int x, int y)
{
    return x >= 0 && x < (int)VCC_MAP_WIDTH && y >= 0 && y < (int)VCC_MAP_HEIGHT;
}

static int blocks_entry(uint8_t tile, vcc_direction direction)
{
    if (tile == VCC_WALL || tile == VCC_HIDDEN_WALL || tile == VCC_POPUP_WALL
        || tile == VCC_BLUE_WALL_REAL || tile == VCC_CLONE_MACHINE)
        return 1;
    if (tile == VCC_THIN_NORTH && direction == VCC_DIR_SOUTH)
        return 1;
    if (tile == VCC_THIN_WEST && direction == VCC_DIR_EAST)
        return 1;
    if (tile == VCC_THIN_SOUTH && direction == VCC_DIR_NORTH)
        return 1;
    if (tile == VCC_THIN_EAST && direction == VCC_DIR_WEST)
        return 1;
    if (tile == VCC_THIN_SE
        && (direction == VCC_DIR_NORTH || direction == VCC_DIR_WEST))
        return 1;
    return 0;
}

static int blocks_exit(uint8_t tile, vcc_direction direction)
{
    return (tile == VCC_THIN_NORTH && direction == VCC_DIR_NORTH)
        || (tile == VCC_THIN_WEST && direction == VCC_DIR_WEST)
        || (tile == VCC_THIN_SOUTH && direction == VCC_DIR_SOUTH)
        || (tile == VCC_THIN_EAST && direction == VCC_DIR_EAST)
        || (tile == VCC_THIN_SE
            && (direction == VCC_DIR_SOUTH || direction == VCC_DIR_EAST));
}

static void toggle_walls(vcc_game *game)
{
    size_t index;
    for (index = 0U; index < VCC_MAP_CELLS; ++index) {
        if (game->terrain[index] == VCC_TOGGLE_WALL)
            game->terrain[index] = VCC_TOGGLE_FLOOR;
        else if (game->terrain[index] == VCC_TOGGLE_FLOOR)
            game->terrain[index] = VCC_TOGGLE_WALL;
    }
}

static int push_block(vcc_game *game, int x, int y, vcc_direction direction)
{
    int next_x = x + direction_x[direction];
    int next_y = y + direction_y[direction];
    size_t from;
    size_t to;
    uint8_t target;
    if (!in_bounds(next_x, next_y))
        return 0;
    from = cell((unsigned)x, (unsigned)y);
    to = cell((unsigned)next_x, (unsigned)next_y);
    if (game->actors[to] != 0U || blocks_entry(game->terrain[to], direction))
        return 0;
    target = game->terrain[to];
    if (target == VCC_WATER) {
        game->terrain[to] = VCC_DIRT;
        game->actors[from] = 0U;
        return 1;
    }
    if (target == VCC_BOMB) {
        game->terrain[to] = VCC_FLOOR;
        game->actors[from] = 0U;
        return 1;
    }
    if (target == VCC_FIRE || target == VCC_SOCKET || target == VCC_EXIT
        || (target >= VCC_BLUE_DOOR && target <= VCC_YELLOW_DOOR))
        return 0;
    game->actors[to] = VCC_BLOCK;
    game->actors[from] = 0U;
    return 1;
}

static int enter_player_tile(vcc_game *game, size_t destination)
{
    uint8_t tile = game->terrain[destination];
    if (tile == VCC_CHIP) {
        if (game->chips_left > 0U)
            --game->chips_left;
        game->terrain[destination] = VCC_FLOOR;
    } else if (tile >= VCC_BLUE_KEY && tile <= VCC_YELLOW_KEY) {
        ++game->keys[tile - VCC_BLUE_KEY];
        game->terrain[destination] = VCC_FLOOR;
    } else if (tile >= VCC_FLIPPERS && tile <= VCC_SUCTION_BOOTS) {
        game->boots[tile - VCC_FLIPPERS] = 1U;
        game->terrain[destination] = VCC_FLOOR;
    } else if (tile >= VCC_BLUE_DOOR && tile <= VCC_YELLOW_DOOR) {
        unsigned key = tile - VCC_BLUE_DOOR;
        if (game->keys[key] == 0U)
            return 0;
        --game->keys[key];
        game->terrain[destination] = VCC_FLOOR;
    } else if (tile == VCC_SOCKET) {
        if (game->chips_left != 0U)
            return 0;
        game->terrain[destination] = VCC_FLOOR;
    } else if (tile == VCC_BLUE_WALL_FAKE) {
        game->terrain[destination] = VCC_FLOOR;
    } else if (tile == VCC_WATER && game->boots[0] == 0U) {
        game->status = VCC_DROWNED;
    } else if (tile == VCC_FIRE && game->boots[1] == 0U) {
        game->status = VCC_BURNED;
    } else if (tile == VCC_BOMB) {
        game->terrain[destination] = VCC_FLOOR;
        game->status = VCC_BOMBED;
    } else if (tile == VCC_THIEF) {
        memset(game->boots, 0, sizeof game->boots);
    } else if (tile == VCC_GREEN_BUTTON) {
        toggle_walls(game);
    } else if (tile == VCC_EXIT) {
        game->status = VCC_WON;
    }
    return 1;
}

int vcc_game_start(vcc_game *game, const vcc_level *level)
{
    size_t index;
    int found_player = 0;
    if (!game || !level)
        return 0;
    memset(game, 0, sizeof *game);
    game->level = level;
    game->chips_left = level->chips_required;
    game->time_left_ticks = (uint32_t)level->time_limit * UINT32_C(20);
    game->status = VCC_PLAYING;
    game->player_direction = VCC_DIR_SOUTH;
    for (index = 0U; index < VCC_MAP_CELLS; ++index) {
        uint8_t top = level->lower[index];
        uint8_t beneath = level->upper[index];
        if (is_player(top)) {
            game->player_x = (uint8_t)(index % VCC_MAP_WIDTH);
            game->player_y = (uint8_t)(index / VCC_MAP_WIDTH);
            game->player_direction = (uint8_t)(top - VCC_CHIP_N);
            game->terrain[index] = beneath;
            found_player = 1;
        } else if (is_actor(top)) {
            game->actors[index] = top;
            game->terrain[index] = beneath;
        } else {
            game->terrain[index] = top;
        }
    }
    return found_player;
}

int vcc_game_move(vcc_game *game, vcc_direction direction)
{
    int x;
    int y;
    size_t source;
    size_t destination;
    if (!game || game->status != VCC_PLAYING || direction > VCC_DIR_EAST)
        return 0;
    game->player_direction = (uint8_t)direction;
    x = (int)game->player_x + direction_x[direction];
    y = (int)game->player_y + direction_y[direction];
    if (!in_bounds(x, y))
        return 0;
    source = cell(game->player_x, game->player_y);
    destination = cell((unsigned)x, (unsigned)y);
    if (blocks_exit(game->terrain[source], direction)
        || blocks_entry(game->terrain[destination], direction))
        return 0;
    if (game->actors[destination] == VCC_BLOCK
        && !push_block(game, x, y, direction))
        return 0;
    if (game->actors[destination] != 0U) {
        game->status = VCC_COLLIDED;
        return 0;
    }
    if (!enter_player_tile(game, destination))
        return 0;
    game->player_x = (uint8_t)x;
    game->player_y = (uint8_t)y;
    return 1;
}

void vcc_game_tick(vcc_game *game, vcc_direction input)
{
    if (!game || game->status != VCC_PLAYING)
        return;
    ++game->ticks;
    if (game->time_left_ticks > 0U && --game->time_left_ticks == 0U) {
        game->status = VCC_TIMEOUT;
        return;
    }
    if (input != VCC_DIR_NONE)
        (void)vcc_game_move(game, input);
}

uint8_t vcc_game_visible_tile(const vcc_game *game, uint8_t x, uint8_t y)
{
    size_t index = cell(x, y);
    if (x == game->player_x && y == game->player_y)
        return (uint8_t)(VCC_CHIP_N + game->player_direction);
    return game->actors[index] != 0U ? game->actors[index] : game->terrain[index];
}

unsigned vcc_tile_sprite(unsigned tile)
{
    return tile;
}
