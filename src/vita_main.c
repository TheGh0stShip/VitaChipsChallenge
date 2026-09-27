#include "vcc/dat.h"
#include "vcc/game.h"

#include <SDL2/SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define VIEW_TILES 16
#define TILE_SIZE 32

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    uint8_t *data;
    long length;
    if (!file || fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0
        || fseek(file, 0, SEEK_SET) != 0) {
        if (file)
            fclose(file);
        return NULL;
    }
    data = malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static void draw_board(SDL_Renderer *renderer, SDL_Texture *tiles, const vcc_game *game)
{
    int camera_x = (int)game->player_x - VIEW_TILES / 2;
    int camera_y = (int)game->player_y - VIEW_TILES / 2;
    int x;
    int y;
    if (camera_x < 0) camera_x = 0;
    if (camera_y < 0) camera_y = 0;
    if (camera_x > (int)VCC_MAP_WIDTH - VIEW_TILES)
        camera_x = (int)VCC_MAP_WIDTH - VIEW_TILES;
    if (camera_y > (int)VCC_MAP_HEIGHT - VIEW_TILES)
        camera_y = (int)VCC_MAP_HEIGHT - VIEW_TILES;
    for (y = 0; y < VIEW_TILES; ++y) {
        for (x = 0; x < VIEW_TILES; ++x) {
            unsigned tile = vcc_game_visible_tile(game,
                (uint8_t)(camera_x + x), (uint8_t)(camera_y + y));
            unsigned sprite = vcc_tile_sprite(tile);
            SDL_Rect source = {(int)(sprite / 16U) * TILE_SIZE,
                (int)(sprite % 16U) * TILE_SIZE, TILE_SIZE, TILE_SIZE};
            SDL_Rect target = {x * TILE_SIZE, y * TILE_SIZE, TILE_SIZE, TILE_SIZE};
            (void)SDL_RenderCopy(renderer, tiles, &source, &target);
        }
    }
}

static void draw_hud(SDL_Renderer *renderer, const vcc_game *game)
{
    SDL_Rect panel = {512, 0, SCREEN_WIDTH - 512, SCREEN_HEIGHT};
    SDL_Rect progress = {552, 96, (int)((400U * (uint32_t)(game->level->chips_required
        - game->chips_left)) / (game->level->chips_required ? game->level->chips_required : 1U)), 24};
    SDL_SetRenderDrawColor(renderer, 18, 18, 28, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 238, 205, 37, 255);
    SDL_RenderFillRect(renderer, &progress);
}

static vcc_direction event_direction(const SDL_Event *event)
{
    if (event->type == SDL_KEYDOWN) {
        if (event->key.keysym.sym == SDLK_UP) return VCC_DIR_NORTH;
        if (event->key.keysym.sym == SDLK_LEFT) return VCC_DIR_WEST;
        if (event->key.keysym.sym == SDLK_DOWN) return VCC_DIR_SOUTH;
        if (event->key.keysym.sym == SDLK_RIGHT) return VCC_DIR_EAST;
    }
    if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) return VCC_DIR_NORTH;
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) return VCC_DIR_WEST;
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) return VCC_DIR_SOUTH;
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) return VCC_DIR_EAST;
    }
    return VCC_DIR_NONE;
}

static vcc_direction held_direction(SDL_GameController *controller)
{
    if (!controller) return VCC_DIR_NONE;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) return VCC_DIR_NORTH;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) return VCC_DIR_WEST;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) return VCC_DIR_SOUTH;
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) return VCC_DIR_EAST;
    return VCC_DIR_NONE;
}

int main(void)
{
    uint8_t *bytes = NULL;
    size_t size = 0U;
    vcc_dat *dat = NULL;
    vcc_game game;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Surface *surface = NULL;
    SDL_Texture *tiles = NULL;
    SDL_GameController *controller = NULL;
    uint16_t level_index = 0U;
    uint32_t next_tick;
    uint32_t next_repeat;
    int running = 1;

    bytes = read_file("app0:/data/CHIPS.DAT", &size);
    dat = malloc(sizeof *dat);
    if (!bytes || !dat || vcc_dat_parse(dat, bytes, size) != VCC_DAT_OK
        || !vcc_game_start(&game, &dat->levels[0]))
        goto cleanup;
    free(bytes);
    bytes = NULL;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0)
        goto cleanup;
    window = SDL_CreateWindow("Vita Chips Challenge", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : NULL;
    surface = SDL_LoadBMP("app0:/data/OBJ32_4.bmp");
    tiles = surface && renderer ? SDL_CreateTextureFromSurface(renderer, surface) : NULL;
    SDL_FreeSurface(surface);
    if (!renderer || !tiles)
        goto cleanup;
    if (SDL_NumJoysticks() > 0 && SDL_IsGameController(0))
        controller = SDL_GameControllerOpen(0);
    next_tick = SDL_GetTicks() + 50U;
    next_repeat = SDL_GetTicks() + 250U;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            vcc_direction direction = event_direction(&event);
            if (event.type == SDL_QUIT)
                running = 0;
            if (direction != VCC_DIR_NONE)
                (void)vcc_game_move(&game, direction);
            if ((event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_r)
                || (event.type == SDL_CONTROLLERBUTTONDOWN
                    && event.cbutton.button == SDL_CONTROLLER_BUTTON_X))
                (void)vcc_game_start(&game, &dat->levels[level_index]);
            if (event.type == SDL_CONTROLLERBUTTONDOWN
                && event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                if (level_index > 0U) --level_index;
                (void)vcc_game_start(&game, &dat->levels[level_index]);
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN
                && event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                if (level_index + 1U < dat->level_count) ++level_index;
                (void)vcc_game_start(&game, &dat->levels[level_index]);
            }
        }
        if ((int32_t)(SDL_GetTicks() - next_tick) >= 0) {
            vcc_game_tick(&game, VCC_DIR_NONE);
            next_tick += 50U;
        }
        if ((int32_t)(SDL_GetTicks() - next_repeat) >= 0) {
            vcc_direction direction = held_direction(controller);
            if (direction != VCC_DIR_NONE)
                (void)vcc_game_move(&game, direction);
            next_repeat = SDL_GetTicks() + 120U;
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        draw_board(renderer, tiles, &game);
        draw_hud(renderer, &game);
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }

cleanup:
    if (controller) SDL_GameControllerClose(controller);
    if (tiles) SDL_DestroyTexture(tiles);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    free(bytes);
    free(dat);
    return 0;
}
