/* SPDX-License-Identifier: GPL-3.0-only */
/* Reconstruction of the Chip's Challenge engine in CHIPS.EXE code segments
 * 3 and 7. Each function names the routine it reproduces; control flow,
 * evaluation order, and list handling follow the original, including its
 * quirks. Coordinates are signed 16-bit as in the original. */
#include "vcc/game.h"

#include <stddef.h>
#include <string.h>

#define NONE_TILE 0xFFU  /* DS:0C6C, "keep the current tile" */

/* Movement rules, six bytes per tile at DS:066C: for Chip, blocks, and
 * monsters an allow byte (0 never, 1 always, 2 tile specific) and an action.
 * Chip actions: 0 plain, 1 pick up, 2 die, 3 remove, 4 enter, 5 slide,
 * 6 trap, 7 teleport. Block and monster actions: 1 enter, 2 die, 4 slide,
 * 5 bomb, 6 trap, 7 teleport. */
typedef struct rule {
    uint8_t chip_allow, chip_action;
    uint8_t block_allow, block_action;
    uint8_t monster_allow, monster_action;
} rule;

static const rule rules[0x70] = {
    [VCC_FLOOR] = {1, 0, 1, 0, 1, 1},
    [VCC_CHIP] = {1, 1, 0, 0, 0, 0},
    [VCC_WATER] = {2, 2, 1, 2, 2, 2},
    [VCC_FIRE] = {2, 2, 1, 1, 2, 2},
    [VCC_THIN_NORTH] = {2, 4, 2, 1, 2, 1},
    [VCC_THIN_WEST] = {2, 4, 2, 1, 2, 1},
    [VCC_THIN_SOUTH] = {2, 4, 2, 1, 2, 1},
    [VCC_THIN_EAST] = {2, 4, 2, 1, 2, 1},
    [VCC_BLOCK] = {2, 0, 0, 0, 0, 0},
    [VCC_DIRT] = {1, 3, 0, 0, 0, 0},
    [VCC_ICE] = {2, 5, 1, 4, 1, 4},
    [VCC_FORCE_SOUTH] = {2, 5, 1, 4, 1, 4},
    [VCC_FORCE_NORTH] = {2, 5, 1, 4, 1, 4},
    [VCC_FORCE_EAST] = {2, 5, 1, 4, 1, 4},
    [VCC_FORCE_WEST] = {2, 5, 1, 4, 1, 4},
    [VCC_EXIT] = {1, 4, 1, 1, 0, 0},
    [VCC_BLUE_DOOR] = {2, 3, 0, 0, 0, 0},
    [VCC_RED_DOOR] = {2, 3, 0, 0, 0, 0},
    [VCC_GREEN_DOOR] = {2, 3, 0, 0, 0, 0},
    [VCC_YELLOW_DOOR] = {2, 3, 0, 0, 0, 0},
    [VCC_ICE_NW] = {2, 5, 2, 4, 2, 4},
    [VCC_ICE_NE] = {2, 5, 2, 4, 2, 4},
    [VCC_ICE_SE] = {2, 5, 2, 4, 2, 4},
    [VCC_ICE_SW] = {2, 5, 2, 4, 2, 4},
    [VCC_BLUE_WALL_FAKE] = {2, 0, 0, 0, 0, 0},
    [VCC_BLUE_WALL_REAL] = {2, 0, 0, 0, 0, 0},
    [VCC_THIEF] = {2, 4, 0, 0, 0, 0},
    [VCC_SOCKET] = {2, 3, 0, 0, 0, 0},
    [VCC_GREEN_BUTTON] = {1, 4, 1, 1, 1, 1},
    [VCC_RED_BUTTON] = {1, 4, 1, 1, 1, 1},
    [VCC_TOGGLE_FLOOR] = {1, 4, 1, 1, 1, 1},
    [VCC_BROWN_BUTTON] = {1, 4, 1, 1, 1, 1},
    [VCC_BLUE_BUTTON] = {1, 4, 1, 1, 1, 1},
    [VCC_TELEPORT] = {1, 7, 1, 7, 1, 7},
    [VCC_BOMB] = {1, 2, 1, 5, 1, 5},
    [VCC_TRAP] = {1, 6, 1, 6, 1, 6},
    [VCC_HIDDEN_WALL] = {2, 0, 0, 0, 0, 0},
    [VCC_GRAVEL] = {1, 4, 1, 1, 0, 0},
    [VCC_POPUP_WALL] = {2, 4, 0, 0, 0, 0},
    [VCC_HINT] = {1, 4, 1, 1, 1, 1},
    [VCC_THIN_SE] = {2, 4, 2, 1, 2, 1},
    [VCC_RANDOM_FORCE] = {2, 5, 1, 4, 0, 0},
    [VCC_CHIP_SWIM_N + 0] = {0, 0, 1, 1, 1, 0},
    [VCC_CHIP_SWIM_N + 1] = {0, 0, 1, 1, 1, 0},
    [VCC_CHIP_SWIM_N + 2] = {0, 0, 1, 1, 1, 0},
    [VCC_CHIP_SWIM_N + 3] = {0, 0, 1, 1, 1, 0},
    [0x40] = {2, 2, 0, 0, 0, 0},
    [0x41] = {2, 2, 0, 0, 0, 0},
    [0x42] = {2, 2, 0, 0, 0, 0},
    [0x43] = {2, 2, 0, 0, 0, 0},
    [0x44] = {2, 2, 0, 0, 0, 0},
    [0x45] = {2, 2, 0, 0, 0, 0},
    [0x46] = {2, 2, 0, 0, 0, 0},
    [0x47] = {2, 2, 0, 0, 0, 0},
    [0x48] = {2, 2, 0, 0, 0, 0},
    [0x49] = {2, 2, 0, 0, 0, 0},
    [0x4A] = {2, 2, 0, 0, 0, 0},
    [0x4B] = {2, 2, 0, 0, 0, 0},
    [0x4C] = {2, 2, 0, 0, 0, 0},
    [0x4D] = {2, 2, 0, 0, 0, 0},
    [0x4E] = {2, 2, 0, 0, 0, 0},
    [0x4F] = {2, 2, 0, 0, 0, 0},
    [0x50] = {2, 2, 0, 0, 0, 0},
    [0x51] = {2, 2, 0, 0, 0, 0},
    [0x52] = {2, 2, 0, 0, 0, 0},
    [0x53] = {2, 2, 0, 0, 0, 0},
    [0x54] = {2, 2, 0, 0, 0, 0},
    [0x55] = {2, 2, 0, 0, 0, 0},
    [0x56] = {2, 2, 0, 0, 0, 0},
    [0x57] = {2, 2, 0, 0, 0, 0},
    [0x58] = {2, 2, 0, 0, 0, 0},
    [0x59] = {2, 2, 0, 0, 0, 0},
    [0x5A] = {2, 2, 0, 0, 0, 0},
    [0x5B] = {2, 2, 0, 0, 0, 0},
    [0x5C] = {2, 2, 0, 0, 0, 0},
    [0x5D] = {2, 2, 0, 0, 0, 0},
    [0x5E] = {2, 2, 0, 0, 0, 0},
    [0x5F] = {2, 2, 0, 0, 0, 0},
    [0x60] = {2, 2, 0, 0, 0, 0},
    [0x61] = {2, 2, 0, 0, 0, 0},
    [0x62] = {2, 2, 0, 0, 0, 0},
    [0x63] = {2, 2, 0, 0, 0, 0},
    [0x64] = {2, 1, 1, 1, 1, 1},
    [0x65] = {2, 1, 1, 1, 1, 1},
    [0x66] = {2, 1, 1, 1, 1, 1},
    [0x67] = {2, 1, 1, 1, 1, 1},
    [0x68] = {2, 1, 1, 1, 0, 0},
    [0x69] = {2, 1, 1, 1, 0, 0},
    [0x6A] = {2, 1, 1, 1, 0, 0},
    [0x6B] = {2, 1, 1, 1, 0, 0},
    [VCC_CHIP_N + 0] = {0, 0, 1, 1, 1, 0},
    [VCC_CHIP_N + 1] = {0, 0, 1, 1, 1, 0},
    [VCC_CHIP_N + 2] = {0, 0, 1, 1, 1, 0},
    [VCC_CHIP_N + 3] = {0, 0, 1, 1, 1, 0},
};

/* ---- Small helpers ------------------------------------------------- */

static size_t at(int x, int y)
{
    return (size_t)(y * 32 + x);
}

static int in_map(int x, int y)
{
    return x >= 0 && y >= 0 && x < 32 && y < 32;
}

static int is_chip_tile(uint8_t tile)
{
    return (tile >= VCC_CHIP_N && tile <= VCC_CHIP_E)
        || (tile >= VCC_CHIP_SWIM_N && tile <= VCC_CHIP_SWIM_N + 3);
}

static int is_ice(uint8_t tile)
{
    return tile == VCC_ICE || (tile >= VCC_ICE_NW && tile <= VCC_ICE_SW);
}

static void sound(vcc_game *game, vcc_sound_id id, int interrupt)
{
    if (game->hooks.sound) game->hooks.sound(game->hooks.context, id, interrupt);
}

static void die(vcc_game *game)
{
    if (game->hooks.died) game->hooks.died(game->hooks.context, game);
}

/* 1:00DC, Microsoft C rand(). */
static unsigned c_rand(vcc_game *game)
{
    game->random_seed = game->random_seed * UINT32_C(0x343FD) + UINT32_C(0x269EC3);
    return (unsigned)((game->random_seed >> 16) & 0x7FFFU);
}

/* 3:072E */
static int random_below(vcc_game *game, unsigned n)
{
    return (int)((uint16_t)c_rand(game) % n);
}

/* 3:00B0, turn left. */
static void turn_left(int dx, int dy, int16_t *ndx, int16_t *ndy)
{
    switch (dx * 2 + dy) {
    case -2: *ndx = 0; *ndy = 1; break;
    case -1: *ndx = -1; *ndy = 0; break;
    case 1: *ndx = 1; *ndy = 0; break;
    case 2: *ndx = 0; *ndy = -1; break;
    default: break;
    }
}

/* 3:0116, turn right. */
static void turn_right(int dx, int dy, int16_t *ndx, int16_t *ndy)
{
    switch (dx * 2 + dy) {
    case -2: *ndx = 0; *ndy = -1; break;
    case -1: *ndx = 1; *ndy = 0; break;
    case 1: *ndx = -1; *ndy = 0; break;
    case 2: *ndx = 0; *ndy = 1; break;
    default: break;
    }
}

/* 3:017C, turn around. */
static void turn_back(int dx, int dy, int16_t *ndx, int16_t *ndy)
{
    *ndx = (int16_t)-dx;
    *ndy = (int16_t)-dy;
}

/* 3:0486, a creature or Chip tile facing (dx, dy). */
static uint8_t facing(uint8_t tile, int dx, int dy)
{
    uint8_t base = (uint8_t)(tile & 0xFCU);
    switch (dx * 2 + dy) {
    case -2: return (uint8_t)(base + 1);
    case -1: return base;
    case 1: return (uint8_t)(base + 2);
    case 2: return (uint8_t)(base + 3);
    default: return tile;
    }
}

/* 3:04D8, direction of a monster tile. */
static int monster_direction(uint8_t tile, int16_t *dx, int16_t *dy)
{
    if (tile < 0x40 || tile > 0x63) return 0;
    switch (tile & 3U) {
    case 0: *dx = 0; *dy = -1; break;
    case 1: *dx = -1; *dy = 0; break;
    case 2: *dx = 0; *dy = 1; break;
    default: *dx = 1; *dy = 0; break;
    }
    return 1;
}

static uint8_t chip_facing(const vcc_game *game, int x, int y, int dx, int dy)
{
    return facing(game->bottom[at(x, y)] == VCC_WATER ? VCC_CHIP_SWIM_N : VCC_CHIP_N, dx, dy);
}

/* ---- Lists --------------------------------------------------------- */

/* 3:0000 */
static int find_monster(const vcc_game *game, int x, int y)
{
    int index;
    for (index = 0; index < (int)game->monster_count; ++index)
        if (game->monsters[index].x == x && game->monsters[index].y == y) return index;
    return -1;
}

/* 3:0058 */
static int find_slip(const vcc_game *game, int x, int y)
{
    int index;
    for (index = 0; index < (int)game->slip_count; ++index)
        if (game->slips[index].x == x && game->slips[index].y == y) return index;
    return -1;
}

static void trap_enter(vcc_game *game, int x, int y, int from_x, int from_y);

/* 3:0228 */
static void add_monster(vcc_game *game, uint8_t tile, int x, int y, int dx, int dy, int on_machine)
{
    size_t cell = at(x, y);
    uint8_t under;
    vcc_mover *monster;
    if (game->monster_count >= VCC_MAX_MOVERS) return;
    under = game->bottom[cell];
    if (!on_machine && under == VCC_CLONE_MACHINE) return;
    monster = &game->monsters[game->monster_count];
    monster->tile = tile;
    monster->x = (int16_t)x;
    monster->y = (int16_t)y;
    monster->dx = (int16_t)dx;
    monster->dy = (int16_t)dy;
    monster->flag = 0;
    if (game->top[cell] != tile) {
        game->bottom[cell] = game->top[cell];
        under = game->bottom[cell];
    }
    game->top[cell] = tile;
    ++game->monster_count;
    if (under == VCC_TRAP)
        trap_enter(game, x, y, -1, -1);
    else if (is_chip_tile(under))
        game->death = VCC_DEATH_MONSTER;
}

/* 3:12BE */
static int remove_slip(vcc_game *game, int x, int y)
{
    int index;
    if (game->slip_count == 0U) return 0;
    index = find_slip(game, x, y);
    if (index < 0) return 0;
    if (game->slips[index].flag != 0) {
        int monster = find_monster(game, x, y);
        if (monster >= 0) game->monsters[monster].flag = 0;
    }
    memmove(&game->slips[index], &game->slips[index + 1],
        (size_t)((int)game->slip_count - index - 1) * sizeof game->slips[0]);
    --game->slip_count;
    return 1;
}

/* 3:03B4 */
static void remove_monster(vcc_game *game, int index)
{
    if (game->monsters[index].flag != 0)
        (void)remove_slip(game, game->monsters[index].x, game->monsters[index].y);
    memmove(&game->monsters[index], &game->monsters[index + 1],
        (size_t)((int)game->monster_count - index - 1) * sizeof game->monsters[0]);
    --game->monster_count;
}

/* ---- Inventory ----------------------------------------------------- */

/* 3:1734 */
static void thief(vcc_game *game, int boots_only)
{
    if (!boots_only) memset(game->keys, 0, sizeof game->keys);
    memset(game->boots, 0, sizeof game->boots);
}

/* 3:1770 */
static void pick_up(vcc_game *game, uint8_t tile)
{
    vcc_sound_id id = VCC_SOUND_TOOL;
    if (tile == VCC_CHIP) {
        if (game->chips_left > 0) --game->chips_left;
        id = VCC_SOUND_CHIP;
    } else if (tile >= VCC_BLUE_KEY && tile <= VCC_YELLOW_KEY) {
        ++game->keys[tile - VCC_BLUE_KEY];
    } else if (tile >= VCC_FLIPPERS && tile <= VCC_SUCTION_BOOTS) {
        ++game->boots[tile - VCC_FLIPPERS];
    }
    sound(game, id, 1);
}

/* 3:1804, the green key is never used up. */
static int open_door(vcc_game *game, uint8_t tile, int apply)
{
    int key = tile - VCC_BLUE_DOOR;
    if (key < 0 || key > 3 || game->keys[key] == 0) return 0;
    if (apply && key != 2) --game->keys[key];
    return 1;
}

/* 3:187C */
static int has_boots(const vcc_game *game, uint8_t tile)
{
    if (tile == VCC_WATER) return game->boots[0] != 0;
    if (tile == VCC_FIRE) return game->boots[1] != 0;
    if (is_ice(tile)) return game->boots[2] != 0;
    if (tile == VCC_FORCE_SOUTH || tile == VCC_FORCE_NORTH || tile == VCC_FORCE_EAST
        || tile == VCC_FORCE_WEST || tile == VCC_RANDOM_FORCE)
        return game->boots[3] != 0;
    return 0;
}

/* 3:1934. Thin walls and ice corners block leaving through their walled
 * edges and entering through the opposite ones. */
static int edge_open(uint8_t tile, int dx, int dy, int entering)
{
    unsigned walls;
    switch (tile) {
    case VCC_THIN_NORTH: walls = 1U; break;
    case VCC_THIN_WEST: walls = 2U; break;
    case VCC_THIN_SOUTH: walls = 4U; break;
    case VCC_THIN_EAST: walls = 8U; break;
    case VCC_ICE_NW: walls = 1U | 2U; break;
    case VCC_ICE_NE: walls = 1U | 8U; break;
    case VCC_ICE_SE: case VCC_THIN_SE: walls = 4U | 8U; break;
    case VCC_ICE_SW: walls = 4U | 2U; break;
    default: return 1;
    }
    if (entering) {
        if ((walls & 1U) && dx == 0 && dy == 1) return 0;
        if ((walls & 2U) && dx == 1 && dy == 0) return 0;
        if ((walls & 4U) && dx == 0 && dy == -1) return 0;
        if ((walls & 8U) && dx == -1 && dy == 0) return 0;
    } else {
        if ((walls & 1U) && dx == 0 && dy == -1) return 0;
        if ((walls & 2U) && dx == -1 && dy == 0) return 0;
        if ((walls & 4U) && dx == 0 && dy == 1) return 0;
        if ((walls & 8U) && dx == 1 && dy == 0) return 0;
    }
    return 1;
}

static int is_edge_tile(uint8_t tile)
{
    return (tile >= VCC_THIN_NORTH && tile <= VCC_THIN_EAST)
        || (tile >= VCC_ICE_NW && tile <= VCC_ICE_SW) || tile == VCC_THIN_SE;
}

/* ---- Traps, buttons, clones, teleports ----------------------------- */

/* 3:22BE */
static int trap_at(const vcc_game *game, int x, int y)
{
    int index;
    for (index = 0; index < (int)game->trap_count; ++index)
        if (game->traps[index].trap_x == x && game->traps[index].trap_y == y) return index;
    return -1;
}

/* 3:2270 */
static int trap_for_button(const vcc_game *game, int x, int y)
{
    int index;
    for (index = 0; index < (int)game->trap_count; ++index)
        if (game->traps[index].button_x == x && game->traps[index].button_y == y) return index;
    return -1;
}

/* 3:206C, the run of consecutive records for one trap. */
static int trap_range(const vcc_game *game, int x, int y, int *first, int *last)
{
    int index = trap_at(game, x, y);
    if (index < 0) return 0;
    *first = index;
    while (index + 1 < (int)game->trap_count && game->traps[index + 1].trap_x == x
        && game->traps[index + 1].trap_y == y)
        ++index;
    *last = index;
    return 1;
}

/* 3:21AA. Something entering a trap is held unless a linked brown button
 * is pressed by something other than the mover leaving it. */
static void trap_enter(vcc_game *game, int x, int y, int from_x, int from_y)
{
    int first;
    int last;
    int index;
    int16_t closed = 1;
    if (!trap_range(game, x, y, &first, &last)) return;
    for (index = first; index <= last; ++index) {
        const vcc_trap *trap = &game->traps[index];
        if (game->top[at(trap->button_x, trap->button_y)] == VCC_BROWN_BUTTON) continue;
        if (trap->button_x == from_x && trap->button_y == from_y) continue;
        closed = 0;
        break;
    }
    for (index = first; index <= last; ++index) game->traps[index].closed = closed;
}

/* 3:211A */
static void brown_button(vcc_game *game, int x, int y, int interrupt)
{
    int index;
    int first;
    int last;
    sound(game, VCC_SOUND_SWITCH, interrupt);
    index = trap_for_button(game, x, y);
    if (index < 0) return;
    if (!trap_range(game, game->traps[index].trap_x, game->traps[index].trap_y, &first, &last))
        return;
    for (index = first; index <= last; ++index) game->traps[index].closed = 0;
}

/* 3:1FAC, no sound in the original. */
static void green_button(vcc_game *game)
{
    unsigned index;
    for (index = 0U; index < game->toggle_count; ++index) {
        size_t cell = at(game->toggles[index].x, game->toggles[index].y);
        if (game->top[cell] == VCC_TOGGLE_WALL) game->top[cell] = VCC_TOGGLE_FLOOR;
        else if (game->top[cell] == VCC_TOGGLE_FLOOR) game->top[cell] = VCC_TOGGLE_WALL;
        if (game->bottom[cell] == VCC_TOGGLE_WALL) game->bottom[cell] = VCC_TOGGLE_FLOOR;
        else if (game->bottom[cell] == VCC_TOGGLE_FLOOR) game->bottom[cell] = VCC_TOGGLE_WALL;
    }
}

/* 3:1E6A. Tanks reverse from the direction their tile shows. */
static void blue_button(vcc_game *game, int interrupt)
{
    unsigned index;
    sound(game, VCC_SOUND_SWITCH, interrupt);
    for (index = 0U; index < game->monster_count; ++index) {
        vcc_mover *monster = &game->monsters[index];
        int16_t dx = 0;
        int16_t dy = 0;
        if (monster->tile < VCC_TANK_N || monster->tile > VCC_TANK_N + 3) continue;
        (void)monster_direction(monster->tile, &dx, &dy);
        turn_left(dx, dy, &dx, &dy);
        turn_left(dx, dy, &dx, &dy);
        monster->dx = dx;
        monster->dy = dy;
        monster->tile = facing(monster->tile, dx, dy);
        game->top[at(monster->x, monster->y)] = monster->tile;
    }
}

static int chip_rule(vcc_game *game, int x, int y, int dx, int dy, int *action,
    int apply, int use_top);
static int block_rule(const vcc_game *game, int x, int y, int dx, int dy, int *action);
static int monster_rule(const vcc_game *game, uint8_t mover, int x, int y, int dx, int dy,
    int *action);
static int block_move(vcc_game *game, int x, int y, int dx, int dy, uint8_t tile,
    int16_t *button);

/* 3:260E */
static int clone_for_button(const vcc_game *game, int x, int y)
{
    int index;
    for (index = 0; index < (int)game->clone_count; ++index)
        if (game->clones[index].button_x == x && game->clones[index].button_y == y) return index;
    return -1;
}

/* 3:2442 */
static void red_button(vcc_game *game, int x, int y, int interrupt)
{
    int index;
    int mx;
    int my;
    uint8_t tile;
    int16_t dx = 0;
    int16_t dy = 0;
    int action;
    sound(game, VCC_SOUND_SWITCH, interrupt);
    index = clone_for_button(game, x, y);
    if (index < 0) return;
    mx = game->clones[index].machine_x;
    my = game->clones[index].machine_y;
    if (!in_map(mx, my)) return;
    tile = game->top[at(mx, my)];
    if (monster_direction(tile, &dx, &dy)) {
        int nx = mx + dx;
        int ny = my + dy;
        if (!in_map(nx, ny)) return;
        if (!monster_rule(game, tile, nx, ny, dx, dy, &action) && game->top[at(nx, ny)] != tile)
            return;
        if (find_monster(game, mx, my) != -1) return;
        add_monster(game, tile, mx, my, dx, dy, 1);
        return;
    }
    switch (tile) {
    case VCC_CLONE_BLOCK_N: dx = 0; dy = -1; break;
    case VCC_CLONE_BLOCK_W: dx = -1; dy = 0; break;
    case VCC_CLONE_BLOCK_S: dx = 0; dy = 1; break;
    case VCC_CLONE_BLOCK_E: dx = 1; dy = 0; break;
    default: break;
    }
    if (!in_map(mx + dx, my + dy)) return;
    if (block_rule(game, mx + dx, my + dy, dx, dy, &action))
        (void)block_move(game, mx, my, dx, dy, VCC_BLOCK, NULL);
}

/* 3:2910 */
static int teleport_index(const vcc_game *game, int x, int y)
{
    int index;
    for (index = 0; index < (int)game->teleport_count; ++index)
        if (game->teleports[index].x == x && game->teleports[index].y == y) return index;
    return -1;
}

/* 3:276A. Searches teleports in reverse reading order for a free one whose
 * exit accepts the mover; kind is 0 Chip, 1 block, 2 monster. */
static void teleport(vcc_game *game, int16_t *x, int16_t *y, int dx, int dy, int kind)
{
    int start = teleport_index(game, *x, *y);
    int index;
    uint8_t mover = game->top[at(*x, *y)];
    if (start < 0) return;
    index = start - 1;
    if (index < 0) index = (int)game->teleport_count - 1;
    if (index == start) return;
    for (;;) {
        int tx = game->teleports[index].x;
        int ty = game->teleports[index].y;
        if (game->top[at(tx, ty)] == VCC_TELEPORT) {
            int nx = tx + dx;
            int ny = ty + dy;
            int action;
            if (in_map(nx, ny)) {
                int found = 0;
                if (kind == 0) {
                    if (game->top[at(nx, ny)] != VCC_BLOCK
                        || block_move(game, nx, ny, dx, dy, NONE_TILE, NULL)) {
                        if (chip_rule(game, nx, ny, dx, dy, &action, 0, 1)) {
                            sound(game, VCC_SOUND_TELEPORT, 1);
                            found = 1;
                        }
                    }
                } else if (kind == 1) {
                    found = block_rule(game, nx, ny, dx, dy, &action);
                } else {
                    found = monster_rule(game, mover, nx, ny, dx, dy, &action);
                }
                if (found) {
                    *x = (int16_t)tx;
                    *y = (int16_t)ty;
                    return;
                }
            }
        }
        if (--index < 0) index = (int)game->teleport_count - 1;
        if (index == start) return;
    }
}

/* ---- Movement rules ------------------------------------------------ */

/* 3:1A56 */
static int chip_rule(vcc_game *game, int x, int y, int dx, int dy, int *action,
    int apply, int use_top)
{
    size_t cell = at(x, y);
    uint8_t tile;
    const rule *r;
    if (game->bottom[cell] == VCC_CLONE_MACHINE) { *action = 0; return 0; }
    tile = use_top ? game->top[cell] : game->bottom[cell];
    r = tile < 0x70 ? &rules[tile] : &rules[VCC_WALL];
    *action = r->chip_action;
    if (r->chip_allow == 1) return 1;
    if (r->chip_allow != 2) { *action = 0; return 0; }
    if ((tile >= 0x40 && tile <= 0x6F) || tile == VCC_BLOCK) {
        int ignored;
        if (chip_rule(game, x, y, dx, dy, &ignored, 0, 0)) return 1;
        *action = 0;
        return 0;
    }
    switch (tile) {
    case VCC_WATER: case VCC_FIRE: case VCC_ICE: case VCC_FORCE_SOUTH:
    case VCC_FORCE_NORTH: case VCC_FORCE_EAST: case VCC_FORCE_WEST: case VCC_RANDOM_FORCE:
        if (has_boots(game, tile)) *action = 4;
        return 1;
    case VCC_ICE_NW: case VCC_ICE_NE: case VCC_ICE_SE: case VCC_ICE_SW:
        if (!edge_open(tile, dx, dy, 1)) break;
        if (has_boots(game, tile)) *action = 4;
        return 1;
    case VCC_THIN_NORTH: case VCC_THIN_WEST: case VCC_THIN_SOUTH: case VCC_THIN_EAST:
    case VCC_THIN_SE:
        if (!edge_open(tile, dx, dy, 1)) break;
        return 1;
    case VCC_BLUE_DOOR: case VCC_RED_DOOR: case VCC_GREEN_DOOR: case VCC_YELLOW_DOOR:
        if (!open_door(game, tile, apply)) break;
        if (apply) sound(game, VCC_SOUND_DOOR, 1);
        return 1;
    case VCC_BLUE_WALL_FAKE:
        if (apply) game->top[cell] = VCC_FLOOR;
        return 1;
    case VCC_BLUE_WALL_REAL:
    case VCC_HIDDEN_WALL:
        if (apply) game->top[cell] = VCC_WALL;
        break;
    case VCC_THIEF:
        if (apply) { sound(game, VCC_SOUND_THIEF, 1); thief(game, 1); }
        return 1;
    case VCC_SOCKET:
        if (game->chips_left != 0) break;
        if (apply) sound(game, VCC_SOUND_SOCKET, 1);
        return 1;
    case VCC_POPUP_WALL:
        if (apply) game->top[cell] = VCC_WALL;
        return 1;
    default:
        break;
    }
    *action = 0;
    return 0;
}

/* 3:1CA4 */
static int block_rule(const vcc_game *game, int x, int y, int dx, int dy, int *action)
{
    size_t cell = at(x, y);
    uint8_t tile;
    const rule *r;
    if (game->bottom[cell] == VCC_CLONE_MACHINE) { *action = 0; return 0; }
    tile = game->top[cell];
    r = tile < 0x70 ? &rules[tile] : &rules[VCC_WALL];
    *action = r->block_action;
    if (r->block_allow == 1) return 1;
    if (r->block_allow == 2 && is_edge_tile(tile) && edge_open(tile, dx, dy, 1)) return 1;
    *action = 0;
    return 0;
}

/* 3:1D4A. Monsters judge Chip's cell by what is under him. */
static int monster_rule(const vcc_game *game, uint8_t mover, int x, int y, int dx, int dy,
    int *action)
{
    size_t cell = at(x, y);
    uint8_t tile;
    const rule *r;
    if (game->bottom[cell] == VCC_CLONE_MACHINE) { *action = 0; return 0; }
    tile = game->top[cell];
    if (is_chip_tile(tile)) tile = game->bottom[cell];
    r = tile < 0x70 ? &rules[tile] : &rules[VCC_WALL];
    *action = r->monster_action;
    if (r->monster_allow == 1) return 1;
    if (r->monster_allow == 2) {
        if (tile == VCC_WATER) {
            if (mover >= VCC_GLIDER_N && mover <= VCC_GLIDER_N + 3) *action = 1;
            return 1;
        }
        if (tile == VCC_FIRE) {
            if ((mover >= VCC_BUG_N && mover <= VCC_BUG_N + 3)
                || (mover >= VCC_WALKER_N && mover <= VCC_WALKER_N + 3)) {
                *action = 0;
                return 0;
            }
            if (mover >= VCC_FIREBALL_N && mover <= VCC_FIREBALL_N + 3) *action = 1;
            return 1;
        }
        if (is_edge_tile(tile) && edge_open(tile, dx, dy, 1)) return 1;
    }
    *action = 0;
    return 0;
}

/* ---- Sliding ------------------------------------------------------- */

/* 3:1250 */
static vcc_mover *add_slip(vcc_game *game)
{
    if (game->slip_count >= VCC_MAX_MOVERS) return NULL;
    return &game->slips[game->slip_count++];
}

/* 7:0636. Records that the mover from (sx, sy) slides from (x, y) and
 * turns its direction by the tile there. Kind 0 is Chip, 1 a block,
 * 2 a monster. */
static void set_slide(vcc_game *game, int sx, int sy, int x, int y, int16_t *dx, int16_t *dy,
    int kind, uint8_t *tile)
{
    int16_t old_dx = *dx;
    int16_t old_dy = *dy;
    int16_t *out_dx;
    int16_t *out_dy;
    vcc_mover *slip = NULL;
    uint8_t ground;
    if (kind == 0) {
        game->sliding = 1;
        out_dx = &game->slide_dx;
        out_dy = &game->slide_dy;
    } else {
        int index = find_slip(game, sx, sy);
        slip = index >= 0 ? &game->slips[index] : add_slip(game);
        if (!slip) return;
        out_dx = &slip->dx;
        out_dy = &slip->dy;
    }
    ground = (x == sx && y == sy) ? game->bottom[at(x, y)] : game->top[at(x, y)];
    if (is_chip_tile(ground)) ground = game->bottom[at(x, y)];
    switch (ground) {
    case VCC_ICE: case VCC_TELEPORT: case VCC_TRAP:
        *out_dx = old_dx; *out_dy = old_dy; break;
    case VCC_FORCE_SOUTH: *out_dx = 0; *out_dy = 1; break;
    case VCC_FORCE_NORTH: *out_dx = 0; *out_dy = -1; break;
    case VCC_FORCE_EAST: *out_dx = 1; *out_dy = 0; break;
    case VCC_FORCE_WEST: *out_dx = -1; *out_dy = 0; break;
    case VCC_ICE_NW:
        if (old_dx == -1 && old_dy == 0) { *out_dx = 0; *out_dy = 1; }
        else if (old_dx == 0 && old_dy == -1) { *out_dx = 1; *out_dy = 0; }
        else { *out_dx = (int16_t)-old_dx; *out_dy = (int16_t)-old_dy; }
        break;
    case VCC_ICE_NE:
        if (old_dx == 0 && old_dy == -1) { *out_dx = -1; *out_dy = 0; }
        else if (old_dx == 1 && old_dy == 0) { *out_dx = 0; *out_dy = 1; }
        else { *out_dx = (int16_t)-old_dx; *out_dy = (int16_t)-old_dy; }
        break;
    case VCC_ICE_SE:
        if (old_dx == 0 && old_dy == 1) { *out_dx = -1; *out_dy = 0; }
        else if (old_dx == 1 && old_dy == 0) { *out_dx = 0; *out_dy = -1; }
        else { *out_dx = (int16_t)-old_dx; *out_dy = (int16_t)-old_dy; }
        break;
    case VCC_ICE_SW:
        if (old_dx == -1 && old_dy == 0) { *out_dx = 0; *out_dy = -1; }
        else if (old_dx == 0 && old_dy == 1) { *out_dx = 1; *out_dy = 0; }
        else { *out_dx = (int16_t)-old_dx; *out_dy = (int16_t)-old_dy; }
        break;
    case VCC_RANDOM_FORCE:
        switch (random_below(game, 4U)) {
        case 0: *out_dx = 0; *out_dy = -1; break;
        case 1: *out_dx = 0; *out_dy = 1; break;
        case 2: *out_dx = -1; *out_dy = 0; break;
        default: *out_dx = 1; *out_dy = 0; break;
        }
        break;
    default:
        break;
    }
    *dx = *out_dx;
    *dy = *out_dy;
    if (kind == 1 || kind == 2) {
        if (kind == 2 && tile && *tile != NONE_TILE) *tile = facing(*tile, *dx, *dy);
        slip->tile = (tile && *tile != NONE_TILE) ? *tile : game->top[at(sx, sy)];
        slip->x = (int16_t)x;
        slip->y = (int16_t)y;
        slip->flag = (int16_t)(kind == 2);
        if (kind == 2) {
            int monster = find_monster(game, sx, sy);
            if (monster >= 0) {
                if (tile && *tile != NONE_TILE) game->monsters[monster].tile = *tile;
                game->monsters[monster].flag = 1;
            }
        }
    }
}

/* ---- Movers -------------------------------------------------------- */

static void press_button(vcc_game *game, uint8_t tile, int x, int y, int by_chip)
{
    switch (tile) {
    case VCC_GREEN_BUTTON: green_button(game); break;
    case VCC_RED_BUTTON: red_button(game, x, y, by_chip); break;
    case VCC_BROWN_BUTTON: brown_button(game, x, y, by_chip); break;
    case VCC_BLUE_BUTTON: blue_button(game, by_chip); break;
    default: break;
    }
}

static int is_button(uint8_t tile)
{
    return tile == VCC_GREEN_BUTTON || tile == VCC_BLUE_BUTTON
        || tile == VCC_BROWN_BUTTON || tile == VCC_RED_BUTTON;
}

/* 7:0DAE. `button` receives {pressed, x, y} for Chip's push so the button
 * is handled after Chip has moved. */
static int block_move(vcc_game *game, int x, int y, int dx, int dy, uint8_t tile,
    int16_t *button)
{
    int16_t nx = (int16_t)(x + dx);
    int16_t ny = (int16_t)(y + dy);
    int16_t sdx = (int16_t)dx;
    int16_t sdy = (int16_t)dy;
    size_t src = at(x, y);
    uint8_t mover;
    int action;
    if (!in_map(nx, ny)) goto fail;
    if (game->bottom[src] == VCC_TRAP) {
        int trap = trap_at(game, x, y);
        if (trap < 0 || game->traps[trap].closed == 1) goto fail;
    }
    if (is_edge_tile(game->bottom[src]) && !edge_open(game->bottom[src], dx, dy, 0)) goto fail;
    mover = game->top[src];
    if (!block_rule(game, nx, ny, dx, dy, &action)) goto fail;
    switch (action) {
    case 1:
        game->bottom[at(nx, ny)] = game->top[at(nx, ny)];
        break;
    case 2:
        sound(game, VCC_SOUND_SPLASH, 1);
        tile = VCC_DIRT;
        break;
    case 4:
        set_slide(game, x, y, nx, ny, &sdx, &sdy, 1, &tile);
        game->bottom[at(nx, ny)] = game->top[at(nx, ny)];
        break;
    case 5:
        sound(game, VCC_SOUND_BOMB, 1);
        tile = VCC_FLOOR;
        break;
    case 6:
        if (find_slip(game, x, y) >= 0) set_slide(game, x, y, nx, ny, &sdx, &sdy, 1, &tile);
        trap_enter(game, nx, ny, x, y);
        game->bottom[at(nx, ny)] = game->top[at(nx, ny)];
        break;
    case 7:
        teleport(game, &nx, &ny, dx, dy, 1);
        set_slide(game, x, y, nx, ny, &sdx, &sdy, 1, &tile);
        game->bottom[at(nx, ny)] = game->top[at(nx, ny)];
        action = 4;
        break;
    default:
        break;
    }
    if (action != 4 && action != 6 && game->slip_count != 0U) (void)remove_slip(game, x, y);
    game->top[at(nx, ny)] = tile != NONE_TILE ? tile : mover;
    if (game->bottom[src] != VCC_CLONE_MACHINE) {
        game->top[src] = game->bottom[src];
        game->bottom[src] = VCC_FLOOR;
    }
    if (action == 1) {
        uint8_t under = game->bottom[at(nx, ny)];
        if (!button) {
            press_button(game, under, nx, ny, 0);
        } else if (is_button(under)) {
            button[0] = 1;
            button[1] = nx;
            button[2] = ny;
        }
    } else if (button) {
        button[0] = 0;
    }
    if (is_chip_tile(game->bottom[at(nx, ny)])) game->death = VCC_DEATH_BLOCK;
    return 1;
fail:
    if (game->slip_count != 0U) (void)remove_slip(game, x, y);
    return 0;
}

/* 7:18DA. Returns 0 blocked, 1 moved, 2 the monster is gone. */
static int monster_move(vcc_game *game, int16_t *x, int16_t *y, int16_t *dx, int16_t *dy,
    uint8_t tile)
{
    int16_t sx = *x;
    int16_t sy = *y;
    int16_t nx = (int16_t)(sx + *dx);
    int16_t ny = (int16_t)(sy + *dy);
    size_t src = at(sx, sy);
    int died = 0;
    int action;
    uint8_t mover;
    if (!in_map(nx, ny)) goto fail;
    if (game->bottom[src] == VCC_TRAP) {
        int trap = trap_at(game, sx, sy);
        if (trap < 0 || game->traps[trap].closed == 1) goto fail;
    }
    if (is_edge_tile(game->bottom[src]) && !edge_open(game->bottom[src], *dx, *dy, 0)) goto fail;
    mover = game->top[src];
    if (!monster_rule(game, mover, nx, ny, *dx, *dy, &action)) goto fail;
    switch (action) {
    case 0: case 1:
        game->bottom[at(nx, ny)] = game->top[at(nx, ny)];
        break;
    case 2:
        died = 1;
        break;
    case 4:
        set_slide(game, sx, sy, nx, ny, dx, dy, 2, &tile);
        game->bottom[at(nx, ny)] = game->top[at(nx, ny)];
        break;
    case 5:
        sound(game, VCC_SOUND_BOMB, 1);
        tile = VCC_FLOOR;
        died = 1;
        break;
    case 6:
        trap_enter(game, nx, ny, sx, sy);
        game->bottom[at(nx, ny)] = game->top[at(nx, ny)];
        break;
    case 7:
        teleport(game, &nx, &ny, *dx, *dy, 2);
        set_slide(game, sx, sy, nx, ny, dx, dy, 2, &tile);
        game->bottom[at(nx, ny)] = game->top[at(nx, ny)];
        action = 4;
        break;
    default:
        break;
    }
    if (action != 4 && game->slip_count != 0U) (void)remove_slip(game, sx, sy);
    if (action != 2) game->top[at(nx, ny)] = tile != NONE_TILE ? tile : mover;
    if (game->bottom[src] != VCC_CLONE_MACHINE) {
        game->top[src] = game->bottom[src];
        game->bottom[src] = VCC_FLOOR;
    }
    if (action == 1) {
        uint8_t under = game->bottom[at(nx, ny)];
        if (under == VCC_BLUE_BUTTON) {
            int monster = find_monster(game, sx, sy);
            if (monster >= 0) {
                game->monsters[monster].x = nx;
                game->monsters[monster].y = ny;
                game->monsters[monster].dx = *dx;
                game->monsters[monster].dy = *dy;
            }
            blue_button(game, 0);
            if (monster >= 0) {
                *dx = game->monsters[monster].dx;
                *dy = game->monsters[monster].dy;
            }
        } else {
            press_button(game, under, nx, ny, 0);
        }
    }
    if (!died && is_chip_tile(game->bottom[at(nx, ny)])) game->death = VCC_DEATH_MONSTER;
    *x = nx;
    *y = ny;
    return died ? 2 : 1;
fail:
    if (game->slip_count != 0U) (void)remove_slip(game, sx, sy);
    return 0;
}

/* 7:1184. `player` marks a key or mouse move, `blocked_sound` enables
 * BlockedMoveSound on failure. */
static int chip_move(vcc_game *game, int dx, int dy, int player, int blocked_sound)
{
    int16_t nx = (int16_t)(game->chip_x + dx);
    int16_t ny = (int16_t)(game->chip_y + dy);
    int16_t mdx = (int16_t)dx;
    int16_t mdy = (int16_t)dy;
    int16_t button[3] = {0, 0, 0};
    uint8_t standing = NONE_TILE;
    int16_t old_x;
    int16_t old_y;
    int ok = 0;
    int action = 0;
    size_t src;
    if (game->paused) return 0;
    if (player) {
        if (game->moved) {
            if (!game->pending) {
                game->pending = 1;
                game->pending_dx = (int16_t)dx;
                game->pending_dy = (int16_t)dy;
            }
            return 0;
        }
        game->pending = 0;
        game->moved = 1;
    }
    game->idle = 0;
    if (!in_map(nx, ny)) goto fail;
    src = at(game->chip_x, game->chip_y);
    if (game->sliding) {
        standing = game->bottom[src];
        if (player) {
            if (is_ice(standing) || standing == VCC_TELEPORT) return 0;
            if ((standing == VCC_FORCE_SOUTH || standing == VCC_FORCE_NORTH
                    || standing == VCC_FORCE_WEST || standing == VCC_FORCE_EAST
                    || standing == VCC_RANDOM_FORCE)
                && game->slide_dx == dx && game->slide_dy == dy)
                return 0;
        }
    }
    if (game->bottom[src] == VCC_TRAP) {
        int trap = trap_at(game, game->chip_x, game->chip_y);
        if (trap < 0 || game->traps[trap].closed == 1) goto fail;
    }
    if (is_edge_tile(game->bottom[src]) && !edge_open(game->bottom[src], dx, dy, 0)) goto fail;
    old_x = game->chip_x;
    old_y = game->chip_y;
    if (game->top[at(nx, ny)] == VCC_BLOCK) {
        int slip = find_slip(game, nx, ny);
        if (slip != -1) {
            int16_t sdx = game->slips[slip].dx;
            int16_t sdy = game->slips[slip].dy;
            if (sdx == dx && sdy == dy) goto fail;
            if (sdx + dx == 0 && sdy + dy == 0) goto fail;
        }
        ok = block_move(game, nx, ny, dx, dy, NONE_TILE, button);
        if (game->death != VCC_ALIVE) goto death;
        if (!ok) goto fail;
    }
    ok = chip_rule(game, nx, ny, dx, dy, &action, 1, 1);
    if (!ok) goto fail;
    game->top[src] = game->bottom[src];
    game->bottom[src] = VCC_FLOOR;
    switch (action) {
    case 1:
        pick_up(game, game->top[at(nx, ny)]);
        goto place;
    case 2:
        switch (game->top[at(nx, ny)]) {
        case VCC_BOMB: game->death = VCC_DEATH_BOMB; break;
        case VCC_WATER: game->death = VCC_DEATH_WATER; break;
        case VCC_FIRE: game->death = VCC_DEATH_FIRE; break;
        default: game->death = VCC_DEATH_MONSTER; break;
        }
        goto death;
    case 4:
        goto enter;
    case 5:
        set_slide(game, game->chip_x, game->chip_y, nx, ny, &mdx, &mdy, 0, NULL);
        if (game->pending && game->top[at(nx, ny)] != standing
            && game->pending_dx + mdx == 0 && game->pending_dy + mdy == 0) {
            game->moved = 1;
            game->pending = 0;
        }
        goto enter;
    case 6:
        trap_enter(game, nx, ny, old_x, old_y);
        goto enter;
    case 7:
        teleport(game, &nx, &ny, dx, dy, 0);
        set_slide(game, game->chip_x, game->chip_y, nx, ny, &mdx, &mdy, 0, NULL);
        action = 5;
        goto enter;
    default:
        goto place;
    }
death:
    game->chip_x = nx;
    game->chip_y = ny;
    game->bottom[at(nx, ny)] = game->top[at(nx, ny)];
    if (game->bottom[at(nx, ny)] == VCC_WATER)
        game->top[at(nx, ny)] = VCC_CHIP_DROWNED;
    else if (game->bottom[at(nx, ny)] == VCC_FIRE)
        game->top[at(nx, ny)] = VCC_CHIP_BURNED;
    else
        game->top[at(nx, ny)] = facing(VCC_CHIP_N, dx, dy);
    sound(game, VCC_SOUND_DEATH, 1);
    die(game);
    return 0;
enter:
    game->bottom[at(nx, ny)] = game->top[at(nx, ny)];
place:
    game->chip_x = nx;
    game->chip_y = ny;
    game->top[at(nx, ny)] = chip_facing(game, nx, ny, dx, dy);
    if (action == 4) press_button(game, game->bottom[at(nx, ny)], nx, ny, 1);
    if (button[0])
        press_button(game, game->bottom[at(button[1], button[2])], button[1], button[2], 0);
    if (game->sliding && action != 5) game->sliding = 0;
    ++game->moves;
    if (game->bottom[at(nx, ny)] == VCC_EXIT) {
        sound(game, VCC_SOUND_COMPLETE, 1);
        if (game->hooks.completed) game->hooks.completed(game->hooks.context, game);
        return 1;
    }
    return ok;
fail:
    game->top[at(game->chip_x, game->chip_y)] =
        chip_facing(game, game->chip_x, game->chip_y, dx, dy);
    if (!ok && blocked_sound) sound(game, VCC_SOUND_BLOCKED, 1);
    return ok;
}

/* ---- Per-tick processing ------------------------------------------ */

static int monster_try(vcc_game *game, int16_t *x, int16_t *y, int16_t *dx, int16_t *dy,
    uint8_t tile)
{
    return monster_move(game, x, y, dx, dy, facing(tile, *dx, *dy));
}

/* Walker and blob fallback: random untried turns from the base direction. */
static int random_turns(vcc_game *game, int16_t *x, int16_t *y, int16_t *ndx, int16_t *ndy,
    uint8_t tile, int base_dx, int base_dy)
{
    unsigned tried = 0U;
    while (tried != 7U) {
        int result;
        switch (random_below(game, 3U)) {
        case 0:
            if (tried & 1U) continue;
            tried |= 1U;
            turn_left(base_dx, base_dy, ndx, ndy);
            break;
        case 1:
            if (tried & 2U) continue;
            tried |= 2U;
            turn_right(base_dx, base_dy, ndx, ndy);
            break;
        default:
            if (tried & 4U) continue;
            tried |= 4U;
            turn_back(base_dx, base_dy, ndx, ndy);
            break;
        }
        result = monster_try(game, x, y, ndx, ndy, tile);
        if (result) return result;
    }
    return 0;
}

static int sign(int value)
{
    return value > 0 ? 1 : (value < 0 ? -1 : 0);
}

static int iabs(int value)
{
    return value < 0 ? -value : value;
}

/* 3:074E. `slow` is set every fourth tick, when teeth and blobs move.
 * The candidate direction persists between monsters, which the original
 * relies on for teeth standing on traps or clone machines. */
static void move_monsters(vcc_game *game, int slow)
{
    int count = (int)game->monster_count;
    int index;
    int16_t ndx = 0;
    int16_t ndy = 0;
    int ddx = 0;
    int ddy = 0;
    for (index = 0; index < count; ++index) {
        vcc_mover *monster = &game->monsters[index];
        int16_t x;
        int16_t y;
        int16_t dx;
        int16_t dy;
        uint8_t tile;
        uint8_t ground;
        int held;
        int result = 0;
        if (monster->flag != 0) continue;
        x = monster->x;
        y = monster->y;
        dx = monster->dx;
        dy = monster->dy;
        tile = monster->tile;
        ground = game->bottom[at(x, y)];
        held = ground == VCC_TRAP || ground == VCC_CLONE_MACHINE;
        switch (tile & 0xFCU) {
        case VCC_BUG_N:
            ndx = dx; ndy = dy;
            if (!held) turn_left(dx, dy, &ndx, &ndy);
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            if (held) break;
            ndx = dx; ndy = dy;
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            turn_right(dx, dy, &ndx, &ndy);
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            turn_back(dx, dy, &ndx, &ndy);
            result = monster_try(game, &x, &y, &ndx, &ndy, tile);
            break;
        case VCC_FIREBALL_N:
            ndx = dx; ndy = dy;
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            if (held) break;
            turn_right(dx, dy, &ndx, &ndy);
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            turn_left(dx, dy, &ndx, &ndy);
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            turn_back(dx, dy, &ndx, &ndy);
            result = monster_try(game, &x, &y, &ndx, &ndy, tile);
            break;
        case VCC_BALL_N:
            ndx = dx; ndy = dy;
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            if (held) break;
            turn_back(dx, dy, &ndx, &ndy);
            result = monster_try(game, &x, &y, &ndx, &ndy, tile);
            break;
        case VCC_TANK_N:
            ndx = dx; ndy = dy;
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            if (game->bottom[at(monster->x, monster->y)] == VCC_TRAP) {
                int trap = trap_at(game, monster->x, monster->y);
                if (trap < 0 || game->traps[trap].closed == 1) break;
            }
            monster->dx = 0;
            monster->dy = 0;
            break;
        case VCC_GLIDER_N:
            ndx = dx; ndy = dy;
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            if (held) break;
            turn_left(dx, dy, &ndx, &ndy);
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            turn_right(dx, dy, &ndx, &ndy);
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            turn_back(dx, dy, &ndx, &ndy);
            result = monster_try(game, &x, &y, &ndx, &ndy, tile);
            break;
        case VCC_TEETH_N:
            if (!slow) break;
            if (!held) {
                ddx = game->chip_x - x;
                ddy = game->chip_y - y;
                if (iabs(ddx) > iabs(ddy)) { ndx = (int16_t)(ddx > 0 ? 1 : -1); ndy = 0; }
                else { ndx = 0; ndy = (int16_t)(ddy > 0 ? 1 : -1); }
            }
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            if (held) break;
            if (iabs(ddy) >= iabs(ddx)) {
                if (ddx != 0) {
                    ndx = (int16_t)sign(ddx); ndy = 0;
                    if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
                }
            } else if (ddy != 0) {
                ndx = 0; ndy = (int16_t)sign(ddy);
                if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            }
            /* 3:0CFB: both refused, so only turn to face Chip. */
            if (iabs(ddy) >= iabs(ddx)) { ndx = 0; ndy = (int16_t)(ddy > 0 ? 1 : -1); }
            else { ndx = (int16_t)(ddx > 0 ? 1 : -1); ndy = 0; }
            monster->tile = facing(tile, ndx, ndy);
            game->top[at(monster->x, monster->y)] = monster->tile;
            break;
        case VCC_WALKER_N:
            ndx = dx; ndy = dy;
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            if (held) break;
            result = random_turns(game, &x, &y, &ndx, &ndy, tile, dx, dy);
            break;
        case VCC_BLOB_N:
            if (!slow) break;
            do {
                ndx = (int16_t)(random_below(game, 3U) - 1);
                ndy = (int16_t)(random_below(game, 3U) - 1);
            } while ((ndx == 0) == (ndy == 0));
            {
                int16_t base_dx = ndx;
                int16_t base_dy = ndy;
                if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
                result = random_turns(game, &x, &y, &ndx, &ndy, tile, base_dx, base_dy);
            }
            break;
        case VCC_PARAMECIUM_N:
            ndx = dx; ndy = dy;
            if (!held) turn_right(dx, dy, &ndx, &ndy);
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            if (held) break;
            ndx = dx; ndy = dy;
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            turn_left(dx, dy, &ndx, &ndy);
            if ((result = monster_try(game, &x, &y, &ndx, &ndy, tile)) != 0) break;
            turn_back(dx, dy, &ndx, &ndy);
            result = monster_try(game, &x, &y, &ndx, &ndy, tile);
            break;
        default:
            break;
        }
        if (result == 1) {
            monster = &game->monsters[index];
            monster->tile = game->top[at(x, y)];
            monster->x = x;
            monster->y = y;
            monster->dx = ndx;
            monster->dy = ndy;
        } else if (result == 2) {
            remove_monster(game, index);
            --index;
            --count;
        }
    }
    if (game->death != VCC_ALIVE) {
        sound(game, VCC_SOUND_DEATH, 1);
        die(game);
    }
}

/* 3:13DE. Advances every sliding block and monster one step. */
static void move_slips(vcc_game *game)
{
    unsigned index = 0U;
    while (index < game->slip_count) {
        unsigned before = game->slip_count;
        vcc_mover slip = game->slips[index];
        int16_t x = slip.x;
        int16_t y = slip.y;
        int16_t dx = slip.dx;
        int16_t dy = slip.dy;
        uint8_t keep = NONE_TILE;
        if (slip.flag != 0) {
            int monster = find_monster(game, x, y);
            int result = monster_move(game, &x, &y, &dx, &dy,
                facing(game->top[at(x, y)], dx, dy));
            if (result == 0) {
                if (is_ice(game->bottom[at(x, y)])) {
                    dx = (int16_t)-dx;
                    dy = (int16_t)-dy;
                    set_slide(game, x, y, x, y, &dx, &dy, 2, &keep);
                    result = monster_move(game, &x, &y, &dx, &dy,
                        facing(game->top[at(x, y)], dx, dy));
                    if (result == 0) {
                        dx = (int16_t)-dx;
                        dy = (int16_t)-dy;
                        set_slide(game, x, y, x, y, &dx, &dy, 2, &keep);
                    }
                } else {
                    set_slide(game, x, y, x, y, &dx, &dy, 2, &keep);
                }
            }
            if (result == 1 && monster >= 0) {
                game->monsters[monster].x = x;
                game->monsters[monster].y = y;
                game->monsters[monster].dx = dx;
                game->monsters[monster].dy = dy;
                game->monsters[monster].tile = facing(game->top[at(x, y)], dx, dy);
            } else if (result == 2 && monster >= 0) {
                remove_monster(game, monster);
            }
        } else if (!block_move(game, x, y, dx, dy, NONE_TILE, NULL)) {
            if (is_ice(game->bottom[at(x, y)])) {
                dx = (int16_t)-dx;
                dy = (int16_t)-dy;
                set_slide(game, x, y, x, y, &dx, &dy, 1, &keep);
                if (!block_move(game, x, y, dx, dy, NONE_TILE, NULL)) {
                    dx = (int16_t)-dx;
                    dy = (int16_t)-dy;
                    set_slide(game, x, y, x, y, &dx, &dy, 1, &keep);
                }
            } else {
                set_slide(game, x, y, x, y, &dx, &dy, 1, &keep);
            }
        }
        if (game->slip_count == before) ++index;
    }
    if (game->death != VCC_ALIVE) {
        sound(game, VCC_SOUND_DEATH, 1);
        die(game);
    }
}

/* Mouse walking: the longer axis first, vertical on ties. */
static void toward(int ddx, int ddy, int16_t *dx, int16_t *dy)
{
    if (iabs(ddy) < iabs(ddx)) { *dx = (int16_t)(ddx > 0 ? 1 : -1); *dy = 0; }
    else { *dx = 0; *dy = (int16_t)(ddy > 0 ? 1 : -1); }
}

static void across(int ddx, int ddy, int16_t *dx, int16_t *dy)
{
    if (iabs(ddy) >= iabs(ddx)) { *dx = (int16_t)sign(ddx); *dy = 0; }
    else { *dx = 0; *dy = (int16_t)sign(ddy); }
}

/* 7:0080 (even ticks) and 7:0400 (every tick); `check_slide` selects the
 * second form, which will not push against Chip's own slide. */
static void mouse_step(vcc_game *game, int check_slide)
{
    int ddx = game->mouse_x - game->chip_x;
    int ddy = game->mouse_y - game->chip_y;
    int16_t dx;
    int16_t dy;
    if (ddx == 0 && ddy == 0) {
        game->mouse_active = 0;
        if (!check_slide) game->moved = 0;
        return;
    }
    toward(ddx, ddy, &dx, &dy);
    if (game->death == VCC_ALIVE
        && !(check_slide && game->sliding && game->slide_dx == dx && game->slide_dy == dy)) {
        int straight = !(ddx != 0 && ddy != 0);
        if (!chip_move(game, dx, dy, 1, straight) && game->mouse_active) {
            across(ddx, ddy, &dx, &dy);
            if (game->death == VCC_ALIVE
                && !(check_slide && game->sliding && game->slide_dx == dx
                    && game->slide_dy == dy)) {
                if (!check_slide) game->moved = 0;
                if (dx != 0 || dy != 0) {
                    if (!chip_move(game, dx, dy, 1, 1)) game->mouse_active = 0;
                } else {
                    game->mouse_active = 0;
                }
            }
        }
    }
    if (game->death != VCC_ALIVE
        || (game->chip_x == game->mouse_x && game->chip_y == game->mouse_y))
        game->mouse_active = 0;
    if (!check_slide) game->moved = 0;
}

/* 7:0000 */
static void tick(vcc_game *game, uint16_t count)
{
    if ((count & 1U) == 0U) {
        if (game->sliding) {
            /* Chip's own move waits while he slides. */
        } else if (game->moved) {
            game->moved = 0;
        } else if (game->pending) {
            if (game->death == VCC_ALIVE)
                (void)chip_move(game, game->pending_dx, game->pending_dy, 1, 1);
            game->pending = 0;
            game->mouse_active = 0;
        } else if (game->mouse_active) {
            mouse_step(game, 0);
        } else if (game->idle++ >= 2) {
            size_t cell = at(game->chip_x, game->chip_y);
            if (game->top[cell] != VCC_CHIP_S && game->top[cell] != VCC_CHIP_SWIM_S)
                game->top[cell] = game->bottom[cell] == VCC_WATER ? VCC_CHIP_SWIM_S : VCC_CHIP_S;
        }
        if (game->monster_count != 0U) move_monsters(game, (count & 3U) == 0U);
    } else if (game->pending && !game->sliding && !game->moved) {
        if (game->death == VCC_ALIVE)
            (void)chip_move(game, game->pending_dx, game->pending_dy, 1, 1);
        game->pending = 0;
        game->mouse_active = 0;
    }
    if (game->sliding) {
        game->idle = 0;
        if (!chip_move(game, game->slide_dx, game->slide_dy, 0, 1) && game->sliding) {
            game->slide_dx = (int16_t)-game->slide_dx;
            game->slide_dy = (int16_t)-game->slide_dy;
            set_slide(game, game->chip_x, game->chip_y, game->chip_x, game->chip_y,
                &game->slide_dx, &game->slide_dy, 0, NULL);
            if (!chip_move(game, game->slide_dx, game->slide_dy, 0, 1)) {
                game->slide_dx = (int16_t)-game->slide_dx;
                game->slide_dy = (int16_t)-game->slide_dy;
                set_slide(game, game->chip_x, game->chip_y, game->chip_x, game->chip_y,
                    &game->slide_dx, &game->slide_dy, 0, NULL);
            }
        }
    }
    if (game->moved) {
        game->moved = 0;
    } else if (game->pending) {
        if (game->death == VCC_ALIVE
            && !(game->sliding && game->slide_dx == game->pending_dx
                && game->slide_dy == game->pending_dy))
            (void)chip_move(game, game->pending_dx, game->pending_dy, 1, 1);
        game->pending = 0;
        game->mouse_active = 0;
    } else if (game->mouse_active) {
        mouse_step(game, 1);
    }
    move_slips(game);
    /* Clock, 7:05BD: a second is ten ticks of the never-reset counter. */
    if (game->time_left > 0 && count != 0U && count % 10U == 0U) {
        --game->time_left;
        if (game->time_left <= 15) sound(game, VCC_SOUND_TICK, 1);
        if (game->time_left == 0) {
            sound(game, VCC_SOUND_DEATH_TIME, 1);
            game->death = VCC_DEATH_TIME;
            die(game);
        }
    }
}

/* ---- Level setup --------------------------------------------------- */

/* 3:054C */
static void init_level_lists(vcc_game *game)
{
    const vcc_level *level = game->level;
    unsigned index;
    int x;
    int y;
    for (index = 0U; index < level->creature_count; ++index) {
        int cx = level->creatures[index].x;
        int cy = level->creatures[index].y;
        uint8_t tile;
        int16_t dx = 0;
        int16_t dy = 0;
        if (!in_map(cx, cy)) continue;
        tile = game->top[at(cx, cy)];
        if (monster_direction(tile, &dx, &dy)) add_monster(game, tile, cx, cy, dx, dy, 0);
    }
    for (y = 0; y < 32; ++y) {
        for (x = 0; x < 32; ++x) {
            uint8_t tile = game->top[at(x, y)];
            if (tile >= 0x40 && tile <= 0x63) tile = game->bottom[at(x, y)];
            if (tile == VCC_TOGGLE_WALL || tile == VCC_TOGGLE_FLOOR) {
                if (game->toggle_count < VCC_MAX_MOVERS) {
                    game->toggles[game->toggle_count].x = (int16_t)x;
                    game->toggles[game->toggle_count].y = (int16_t)y;
                    ++game->toggle_count;
                }
            } else if (tile == VCC_TELEPORT) {
                if (game->teleport_count < VCC_MAX_MOVERS) {
                    game->teleports[game->teleport_count].x = (int16_t)x;
                    game->teleports[game->teleport_count].y = (int16_t)y;
                    ++game->teleport_count;
                }
            } else if (tile >= VCC_CHIP_N && tile <= VCC_CHIP_E) {
                game->chip_x = (int16_t)x;
                game->chip_y = (int16_t)y;
            }
        }
    }
    if (game->chip_x == -1 || game->chip_y == -1) {
        game->chip_x = 1;
        game->chip_y = 1;
        game->top[at(1, 1)] = game->bottom[at(1, 1)];
        game->bottom[at(1, 1)] = VCC_FLOOR;
    }
}

void vcc_game_seed(vcc_game *game, uint32_t seed)
{
    game->random_seed = seed & 0xFFFFU;
}

int vcc_game_start(vcc_game *game, const vcc_level *level)
{
    uint16_t ticks;
    uint32_t seed;
    vcc_game_hooks hooks;
    unsigned index;
    if (!game || !level) return 0;
    ticks = game->ticks;
    seed = game->random_seed;
    hooks = game->hooks;
    memset(game, 0, sizeof *game);
    game->ticks = ticks;
    game->random_seed = seed;
    game->hooks = hooks;
    game->level = level;
    memcpy(game->top, level->lower, sizeof game->top);
    memcpy(game->bottom, level->upper, sizeof game->bottom);
    game->time_left = (int16_t)level->time_limit;
    game->chips_left = (int16_t)level->chips_required;
    game->chip_x = -1;
    game->chip_y = -1;
    for (index = 0U; index < level->trap_count && index < VCC_MAX_TRAPS; ++index) {
        game->traps[index].button_x = (int16_t)level->traps[index].button_x;
        game->traps[index].button_y = (int16_t)level->traps[index].button_y;
        game->traps[index].trap_x = (int16_t)level->traps[index].trap_x;
        game->traps[index].trap_y = (int16_t)level->traps[index].trap_y;
        game->traps[index].closed = (int16_t)level->traps[index].initially_open;
    }
    game->trap_count = (uint16_t)index;
    for (index = 0U; index < level->clone_count && index < VCC_MAX_CLONES; ++index) {
        game->clones[index].button_x = (int16_t)level->clones[index].button_x;
        game->clones[index].button_y = (int16_t)level->clones[index].button_y;
        game->clones[index].machine_x = (int16_t)level->clones[index].machine_x;
        game->clones[index].machine_y = (int16_t)level->clones[index].machine_y;
    }
    game->clone_count = (uint16_t)index;
    init_level_lists(game);
    game->waiting = 1;
    return 1;
}

void vcc_game_key(vcc_game *game, vcc_direction direction)
{
    static const int8_t step_x[4] = {0, -1, 0, 1};
    static const int8_t step_y[4] = {-1, 0, 1, 0};
    game->waiting = 0;
    if (direction <= VCC_DIR_EAST)
        (void)chip_move(game, step_x[direction], step_y[direction], 1, 1);
}

void vcc_game_click(vcc_game *game, int x, int y)
{
    game->waiting = 0;
    game->mouse_active = 1;
    game->mouse_x = (int16_t)x;
    game->mouse_y = (int16_t)y;
}

void vcc_game_tick(vcc_game *game)
{
    if (game->waiting || game->paused) return;
    ++game->ticks;
    tick(game, game->ticks);
}

uint8_t vcc_game_terrain_tile(const vcc_game *game, uint8_t x, uint8_t y)
{
    return game->bottom[at(x, y)];
}

uint8_t vcc_game_actor_tile(const vcc_game *game, uint8_t x, uint8_t y)
{
    return game->top[at(x, y)];
}

uint8_t vcc_game_tile(const vcc_game *game, uint8_t x, uint8_t y, int bottom)
{
    return bottom ? game->bottom[at(x, y)] : game->top[at(x, y)];
}

unsigned vcc_tile_sprite(unsigned tile)
{
    return tile;
}
