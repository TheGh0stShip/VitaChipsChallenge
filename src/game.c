/* SPDX-License-Identifier: GPL-3.0-only */
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
    return tile == VCC_BLOCK
        || (tile >= VCC_CLONE_BLOCK_N && tile <= VCC_CLONE_BLOCK_E)
        || is_creature(tile) || is_player(tile);
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
    if (tile == VCC_WALL || tile == VCC_HIDDEN_WALL
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

static vcc_direction forced_direction(vcc_game *game)
{
    uint8_t tile = game->terrain[cell(game->player_x, game->player_y)];
    vcc_direction direction = (vcc_direction)game->player_direction;
    if (tile >= VCC_FORCE_SOUTH && tile <= VCC_FORCE_WEST && tile != VCC_CLONE_BLOCK_N
        && tile != VCC_CLONE_BLOCK_W && tile != VCC_CLONE_BLOCK_S
        && tile != VCC_CLONE_BLOCK_E && game->boots[3] == 0U) {
        if (tile == VCC_FORCE_SOUTH) return VCC_DIR_SOUTH;
        if (tile == VCC_FORCE_NORTH) return VCC_DIR_NORTH;
        if (tile == VCC_FORCE_EAST) return VCC_DIR_EAST;
        if (tile == VCC_FORCE_WEST) return VCC_DIR_WEST;
    }
    if (tile == VCC_RANDOM_FORCE && game->boots[3] == 0U) {
        game->random_state = game->random_state * UINT32_C(1103515245) + UINT32_C(12345);
        return (vcc_direction)((game->random_state >> 16U) & 3U);
    }
    if (tile == VCC_ICE && game->boots[2] == 0U)
        return direction;
    if (game->boots[2] != 0U)
        return VCC_DIR_NONE;
    if (tile == VCC_ICE_NW) {
        if (direction == VCC_DIR_NORTH) return VCC_DIR_EAST;
        if (direction == VCC_DIR_WEST) return VCC_DIR_SOUTH;
    } else if (tile == VCC_ICE_NE) {
        if (direction == VCC_DIR_NORTH) return VCC_DIR_WEST;
        if (direction == VCC_DIR_EAST) return VCC_DIR_SOUTH;
    } else if (tile == VCC_ICE_SE) {
        if (direction == VCC_DIR_SOUTH) return VCC_DIR_WEST;
        if (direction == VCC_DIR_EAST) return VCC_DIR_NORTH;
    } else if (tile == VCC_ICE_SW) {
        if (direction == VCC_DIR_SOUTH) return VCC_DIR_EAST;
        if (direction == VCC_DIR_WEST) return VCC_DIR_NORTH;
    }
    return VCC_DIR_NONE;
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

static void reverse_tanks(vcc_game *game)
{
    size_t index;
    for (index = 0U; index < VCC_MAP_CELLS; ++index) {
        uint8_t actor = game->actors[index];
        if (actor >= VCC_TANK_N && actor < VCC_TANK_N + 4U)
            game->actors[index] = (uint8_t)(VCC_TANK_N + ((actor - VCC_TANK_N + 2U) & 3U));
    }
}

static vcc_direction actor_direction(uint8_t actor)
{
    if (actor >= VCC_CLONE_BLOCK_N && actor <= VCC_CLONE_BLOCK_E)
        return (vcc_direction)(actor - VCC_CLONE_BLOCK_N);
    return (vcc_direction)(actor & 3U);
}

static void activate_clone(vcc_game *game, unsigned button_x, unsigned button_y)
{
    unsigned link;
    for (link = 0U; link < game->level->clone_count; ++link) {
        const vcc_clone_link *entry = &game->level->clones[link];
        size_t machine;
        size_t destination;
        uint8_t actor;
        vcc_direction direction;
        int x;
        int y;
        if (entry->button_x != button_x || entry->button_y != button_y
            || entry->machine_x >= VCC_MAP_WIDTH || entry->machine_y >= VCC_MAP_HEIGHT)
            continue;
        machine = cell(entry->machine_x, entry->machine_y);
        actor = game->actors[machine];
        if (!is_actor(actor))
            continue;
        direction = actor_direction(actor);
        x = (int)entry->machine_x + direction_x[direction];
        y = (int)entry->machine_y + direction_y[direction];
        if (!in_bounds(x, y))
            continue;
        destination = cell((unsigned)x, (unsigned)y);
        if (game->actors[destination] == 0U
            && !blocks_entry(game->terrain[destination], direction)) {
            game->actors[destination] = actor;
            if (is_creature(actor) && game->creature_count < VCC_MAX_CREATURES) {
                game->creature_order[game->creature_count].x = (uint8_t)x;
                game->creature_order[game->creature_count].y = (uint8_t)y;
                ++game->creature_count;
            }
        }
    }
}

static int position_occupied(const vcc_game *game, unsigned x, unsigned y)
{
    return (game->player_x == x && game->player_y == y)
        || game->actors[cell(x, y)] != 0U;
}

static int trap_is_open(const vcc_game *game, unsigned trap_x, unsigned trap_y)
{
    unsigned link;
    for (link = 0U; link < game->level->trap_count; ++link) {
        const vcc_trap_link *entry = &game->level->traps[link];
        if (entry->trap_x == trap_x && entry->trap_y == trap_y) {
            if (entry->initially_open != 0U)
                return 1;
            if (entry->button_x < VCC_MAP_WIDTH && entry->button_y < VCC_MAP_HEIGHT
                && position_occupied(game, entry->button_x, entry->button_y))
                return 1;
        }
    }
    return 0;
}

static int creature_can_enter(const vcc_game *game, size_t source, size_t destination,
    vcc_direction direction)
{
    uint8_t tile = game->terrain[destination];
    if (game->actors[destination] != 0U || blocks_exit(game->terrain[source], direction)
        || blocks_entry(tile, direction))
        return 0;
    if (tile == VCC_SOCKET || tile == VCC_THIEF || tile == VCC_GRAVEL
        || (tile >= VCC_BLUE_DOOR && tile <= VCC_YELLOW_DOOR))
        return 0;
    if (game->terrain[source] == VCC_TRAP) {
        unsigned x = (unsigned)(source % VCC_MAP_WIDTH);
        unsigned y = (unsigned)(source / VCC_MAP_WIDTH);
        if (!trap_is_open(game, x, y))
            return 0;
    }
    return 1;
}

static void creature_enter(vcc_game *game, size_t destination, uint8_t actor)
{
    uint8_t terrain = game->terrain[destination];
    uint8_t base = (uint8_t)(actor & UINT8_C(0xfc));
    if (game->player_x == destination % VCC_MAP_WIDTH
        && game->player_y == destination / VCC_MAP_WIDTH) {
        game->status = VCC_COLLIDED;
        return;
    }
    if (terrain == VCC_WATER && base != VCC_GLIDER_N)
        return;
    if (terrain == VCC_FIRE && base != VCC_FIREBALL_N)
        return;
    if (terrain == VCC_BOMB) {
        game->terrain[destination] = VCC_FLOOR;
        return;
    }
    game->actors[destination] = actor;
    if (terrain == VCC_GREEN_BUTTON)
        toggle_walls(game);
    else if (terrain == VCC_BLUE_BUTTON)
        reverse_tanks(game);
    else if (terrain == VCC_RED_BUTTON)
        activate_clone(game, (unsigned)(destination % VCC_MAP_WIDTH),
            (unsigned)(destination / VCC_MAP_WIDTH));
}

static int attempt_creature_move(vcc_game *game, vcc_position *position,
    vcc_direction direction)
{
    size_t source = cell(position->x, position->y);
    uint8_t actor = game->actors[source];
    int x = (int)position->x + direction_x[direction];
    int y = (int)position->y + direction_y[direction];
    size_t destination;
    if (!is_creature(actor) || !in_bounds(x, y))
        return 0;
    destination = cell((unsigned)x, (unsigned)y);
    if (!creature_can_enter(game, source, destination, direction))
        return 0;
    game->actors[source] = 0U;
    actor = (uint8_t)((actor & UINT8_C(0xfc)) | (uint8_t)direction);
    creature_enter(game, destination, actor);
    if (game->actors[destination] == actor) {
        position->x = (uint8_t)x;
        position->y = (uint8_t)y;
    } else {
        position->x = UINT8_MAX;
        position->y = UINT8_MAX;
    }
    return 1;
}

static void move_creature(vcc_game *game, vcc_position *position)
{
    uint8_t actor;
    uint8_t base;
    vcc_direction facing;
    vcc_direction choices[4];
    unsigned count = 4U;
    unsigned index;
    size_t location;
    if (position->x >= VCC_MAP_WIDTH || position->y >= VCC_MAP_HEIGHT)
        return;
    location = cell(position->x, position->y);
    actor = game->actors[location];
    if (!is_creature(actor))
        return;
    base = (uint8_t)(actor & UINT8_C(0xfc));
    facing = (vcc_direction)(actor & 3U);
    if (base == VCC_BUG_N) {
        choices[0] = (vcc_direction)((facing + 1U) & 3U);
        choices[1] = facing;
        choices[2] = (vcc_direction)((facing + 3U) & 3U);
        choices[3] = (vcc_direction)((facing + 2U) & 3U);
    } else if (base == VCC_PARAMECIUM_N) {
        choices[0] = (vcc_direction)((facing + 3U) & 3U);
        choices[1] = facing;
        choices[2] = (vcc_direction)((facing + 1U) & 3U);
        choices[3] = (vcc_direction)((facing + 2U) & 3U);
    } else if (base == VCC_FIREBALL_N) {
        choices[0] = facing;
        choices[1] = (vcc_direction)((facing + 3U) & 3U);
        choices[2] = (vcc_direction)((facing + 1U) & 3U);
        choices[3] = (vcc_direction)((facing + 2U) & 3U);
    } else if (base == VCC_GLIDER_N) {
        choices[0] = facing;
        choices[1] = (vcc_direction)((facing + 1U) & 3U);
        choices[2] = (vcc_direction)((facing + 3U) & 3U);
        choices[3] = (vcc_direction)((facing + 2U) & 3U);
    } else if (base == VCC_BALL_N || base == VCC_TANK_N) {
        choices[0] = facing;
        choices[1] = (vcc_direction)((facing + 2U) & 3U);
        count = base == VCC_TANK_N ? 1U : 2U;
    } else if (base == VCC_TEETH_N) {
        int dx = (int)game->player_x - (int)position->x;
        int dy = (int)game->player_y - (int)position->y;
        choices[0] = dx < 0 ? VCC_DIR_WEST : VCC_DIR_EAST;
        choices[1] = dy < 0 ? VCC_DIR_NORTH : VCC_DIR_SOUTH;
        if (dx == 0) choices[0] = choices[1];
        if (dy == 0) choices[1] = choices[0];
        count = 2U;
    } else {
        game->random_state = game->random_state * UINT32_C(1103515245) + UINT32_C(12345);
        choices[0] = (vcc_direction)((game->random_state >> 16U) & 3U);
        choices[1] = facing;
        choices[2] = (vcc_direction)((facing + 1U) & 3U);
        choices[3] = (vcc_direction)((facing + 3U) & 3U);
    }
    for (index = 0U; index < count; ++index) {
        if (attempt_creature_move(game, position, choices[index]))
            return;
    }
}

static void move_creatures(vcc_game *game)
{
    unsigned index;
    unsigned count = game->creature_count;
    for (index = 0U; index < count && game->status == VCC_PLAYING; ++index)
        move_creature(game, &game->creature_order[index]);
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
        game->last_event = VCC_EVENT_WATER;
        return 1;
    }
    if (target == VCC_BOMB) {
        game->terrain[to] = VCC_FLOOR;
        game->actors[from] = 0U;
        game->last_event = VCC_EVENT_BOMB;
        return 1;
    }
    if (target == VCC_FIRE || target == VCC_SOCKET || target == VCC_EXIT
        || (target >= VCC_BLUE_DOOR && target <= VCC_YELLOW_DOOR))
        return 0;
    game->actors[to] = VCC_BLOCK;
    game->actors[from] = 0U;
    if (target == VCC_GREEN_BUTTON)
        toggle_walls(game);
    else if (target == VCC_BLUE_BUTTON)
        reverse_tanks(game);
    else if (target == VCC_RED_BUTTON)
        activate_clone(game, (unsigned)next_x, (unsigned)next_y);
    return 1;
}

static int enter_player_tile(vcc_game *game, size_t destination)
{
    uint8_t tile = game->terrain[destination];
    if (tile == VCC_CHIP) {
        if (game->chips_left > 0U)
            --game->chips_left;
        game->terrain[destination] = VCC_FLOOR;
        game->last_event = VCC_EVENT_PICKUP;
    } else if (tile >= VCC_BLUE_KEY && tile <= VCC_YELLOW_KEY) {
        ++game->keys[tile - VCC_BLUE_KEY];
        game->terrain[destination] = VCC_FLOOR;
        game->last_event = VCC_EVENT_PICKUP;
    } else if (tile >= VCC_FLIPPERS && tile <= VCC_SUCTION_BOOTS) {
        game->boots[tile - VCC_FLIPPERS] = 1U;
        game->terrain[destination] = VCC_FLOOR;
        game->last_event = VCC_EVENT_PICKUP;
    } else if (tile >= VCC_BLUE_DOOR && tile <= VCC_YELLOW_DOOR) {
        unsigned key = tile - VCC_BLUE_DOOR;
        if (game->keys[key] == 0U)
            return 0;
        if (key != 2U)
            --game->keys[key];
        game->terrain[destination] = VCC_FLOOR;
        game->last_event = VCC_EVENT_DOOR;
    } else if (tile == VCC_SOCKET) {
        if (game->chips_left != 0U)
            return 0;
        game->terrain[destination] = VCC_FLOOR;
    } else if (tile == VCC_BLUE_WALL_FAKE) {
        game->terrain[destination] = VCC_FLOOR;
    } else if (tile == VCC_DIRT) {
        game->terrain[destination] = VCC_FLOOR;
    } else if (tile == VCC_WATER && game->boots[0] == 0U) {
        game->status = VCC_DROWNED;
        game->last_event = VCC_EVENT_WATER;
    } else if (tile == VCC_FIRE && game->boots[1] == 0U) {
        game->status = VCC_BURNED;
        game->last_event = VCC_EVENT_FIRE;
    } else if (tile == VCC_BOMB) {
        game->terrain[destination] = VCC_FLOOR;
        game->status = VCC_BOMBED;
        game->last_event = VCC_EVENT_BOMB;
    } else if (tile == VCC_THIEF) {
        memset(game->boots, 0, sizeof game->boots);
    } else if (tile == VCC_GREEN_BUTTON) {
        toggle_walls(game);
        game->last_event = VCC_EVENT_BUTTON;
    } else if (tile == VCC_RED_BUTTON) {
        activate_clone(game, (unsigned)(destination % VCC_MAP_WIDTH),
            (unsigned)(destination / VCC_MAP_WIDTH));
        game->last_event = VCC_EVENT_BUTTON;
    } else if (tile == VCC_BLUE_BUTTON) {
        reverse_tanks(game);
        game->last_event = VCC_EVENT_BUTTON;
    } else if (tile == VCC_EXIT) {
        game->status = VCC_WON;
        game->last_event = VCC_EVENT_COMPLETE;
    }
    return 1;
}

static void teleport_player(vcc_game *game, vcc_direction direction)
{
    size_t entered = cell(game->player_x, game->player_y);
    size_t offset;
    for (offset = 1U; offset < VCC_MAP_CELLS; ++offset) {
        size_t portal = (entered + VCC_MAP_CELLS - offset) % VCC_MAP_CELLS;
        int portal_x;
        int portal_y;
        int exit_x;
        int exit_y;
        size_t exit_cell;
        if (game->terrain[portal] != VCC_TELEPORT)
            continue;
        portal_x = (int)(portal % VCC_MAP_WIDTH);
        portal_y = (int)(portal / VCC_MAP_WIDTH);
        exit_x = portal_x + direction_x[direction];
        exit_y = portal_y + direction_y[direction];
        if (!in_bounds(exit_x, exit_y))
            continue;
        exit_cell = cell((unsigned)exit_x, (unsigned)exit_y);
        if (game->actors[exit_cell] != 0U
            || blocks_entry(game->terrain[exit_cell], direction))
            continue;
        game->player_x = (uint8_t)exit_x;
        game->player_y = (uint8_t)exit_y;
        (void)enter_player_tile(game, exit_cell);
        game->last_event = VCC_EVENT_TELEPORT;
        return;
    }
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
    game->random_state = (uint32_t)level->number * UINT32_C(1103515245) + UINT32_C(12345);
    game->creature_count = level->creature_count;
    memcpy(game->creature_order, level->creatures,
        (size_t)level->creature_count * sizeof game->creature_order[0]);
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
    game->last_event = VCC_EVENT_NONE;
    {
        vcc_direction forced = forced_direction(game);
        if (forced != VCC_DIR_NONE)
            direction = forced;
    }
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
    if (game->terrain[source] == VCC_TRAP
        && !trap_is_open(game, game->player_x, game->player_y))
        return 0;
    if ((game->actors[destination] == VCC_BLOCK
            || (game->actors[destination] >= VCC_CLONE_BLOCK_N
                && game->actors[destination] <= VCC_CLONE_BLOCK_E))
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
    game->timer_started = 1U;
    if (game->terrain[source] == VCC_POPUP_WALL)
        game->terrain[source] = VCC_WALL;
    if (game->terrain[destination] == VCC_TELEPORT)
        teleport_player(game, direction);
    return 1;
}

void vcc_game_tick(vcc_game *game, vcc_direction input)
{
    if (!game || game->status != VCC_PLAYING)
        return;
    ++game->ticks;
    if (game->timer_started != 0U && game->level->time_limit != 0U
        && game->time_left_ticks > 0U) {
        --game->time_left_ticks;
        if ((game->time_left_ticks % 20U) == 0U && game->time_left_ticks <= 300U)
            game->last_event = VCC_EVENT_CLOCK;
        if (game->time_left_ticks == 0U) {
            game->status = VCC_TIMEOUT;
            return;
        }
    }
    if (input != VCC_DIR_NONE)
        (void)vcc_game_move(game, input);
    else if ((game->ticks & 1U) == 0U) {
        vcc_direction forced = forced_direction(game);
        if (forced != VCC_DIR_NONE && !vcc_game_move(game, forced)) {
            uint8_t tile = game->terrain[cell(game->player_x, game->player_y)];
            if (tile >= VCC_ICE && tile <= VCC_ICE_SW) {
                game->player_direction = (uint8_t)((forced + 2U) & 3U);
                (void)vcc_game_move(game, (vcc_direction)game->player_direction);
            }
        }
    }
    if ((game->ticks & 3U) == 0U && game->status == VCC_PLAYING)
        move_creatures(game);
}

uint8_t vcc_game_visible_tile(const vcc_game *game, uint8_t x, uint8_t y)
{
    size_t index = cell(x, y);
    if (x == game->player_x && y == game->player_y)
        return (uint8_t)(VCC_CHIP_N + game->player_direction);
    return game->actors[index] != 0U ? game->actors[index] : game->terrain[index];
}

uint8_t vcc_game_terrain_tile(const vcc_game *game, uint8_t x, uint8_t y)
{
    return game->terrain[cell(x, y)];
}

uint8_t vcc_game_actor_tile(const vcc_game *game, uint8_t x, uint8_t y)
{
    size_t index = cell(x, y);
    if (x == game->player_x && y == game->player_y)
        return (uint8_t)(VCC_CHIP_N + game->player_direction);
    return game->actors[index];
}

unsigned vcc_tile_sprite(unsigned tile)
{
    return tile;
}

vcc_event vcc_game_take_event(vcc_game *game)
{
    vcc_event event = game->last_event;
    game->last_event = VCC_EVENT_NONE;
    return event;
}
