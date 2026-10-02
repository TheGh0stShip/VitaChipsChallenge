/* SPDX-License-Identifier: GPL-3.0-only */
#include "vcc/dat.h"
#include "vcc/game.h"
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define VIEW_TILES 9
#define SOURCE_TILE_SIZE 32
#define BOARD_TILE_SIZE 60
#define BOARD_X 2
#define BOARD_Y 2
#define INFO_X 623
#define INFO_Y 22
#define INFO_SCALE_NUMERATOR 5
#define INFO_SCALE_DENOMINATOR 3

typedef struct vcc_graphics {
    SDL_Texture *tiles;
    SDL_Texture *actors;
    SDL_Texture *background;
    SDL_Texture *info;
    SDL_Texture *digits;
} vcc_graphics;

typedef struct vcc_sound {
    uint8_t *data;
    uint32_t length;
} vcc_sound;

typedef struct vcc_audio {
    SDL_AudioDeviceID device;
    SDL_AudioSpec format;
    vcc_sound sounds[11];
} vcc_audio;

static const char *const sound_files[11] = {
    NULL, "BLIP2.WAV", "DOOR.WAV", "OOF3.WAV", "WATER2.WAV",
    "STRIKE.WAV", "STRIKE.WAV", "TELEPORT.WAV", "DITTY1.WAV", "CLICK3.WAV",
    "CLICK3.WAV"
};

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    uint8_t *data;
    long length;
    if (!file || fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0
        || fseek(file, 0, SEEK_SET) != 0) {
        if (file) fclose(file);
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

static SDL_Texture *load_bmp(SDL_Renderer *renderer, const char *path)
{
    SDL_Surface *surface = SDL_LoadBMP(path);
    SDL_Texture *texture = surface ? SDL_CreateTextureFromSurface(renderer, surface) : NULL;
    SDL_FreeSurface(surface);
    return texture;
}

static SDL_Texture *load_masked_tiles(SDL_Renderer *renderer)
{
    SDL_Surface *surface = SDL_LoadBMP("app0:/data/OBJ32_MASKED.bmp");
    SDL_Texture *texture;
    if (!surface) return NULL;
    (void)SDL_SetColorKey(surface, SDL_TRUE,
        SDL_MapRGB(surface->format, 1U, 2U, 3U));
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

static int load_sound(vcc_audio *audio, unsigned index)
{
    char path[64];
    SDL_AudioSpec source_format;
    uint8_t *source = NULL;
    uint32_t source_length = 0U;
    SDL_AudioCVT conversion;
    int result;
    if (!sound_files[index]) return 1;
    (void)snprintf(path, sizeof path, "app0:/data/%s", sound_files[index]);
    if (!SDL_LoadWAV(path, &source_format, &source, &source_length)) return 0;
    result = SDL_BuildAudioCVT(&conversion, source_format.format, source_format.channels,
        source_format.freq, audio->format.format, audio->format.channels, audio->format.freq);
    if (result < 0) {
        SDL_FreeWAV(source);
        return 0;
    }
    conversion.len = (int)source_length;
    conversion.buf = SDL_malloc((size_t)conversion.len * (size_t)conversion.len_mult);
    if (!conversion.buf) {
        SDL_FreeWAV(source);
        return 0;
    }
    SDL_memcpy(conversion.buf, source, source_length);
    SDL_FreeWAV(source);
    if (result != 0 && SDL_ConvertAudio(&conversion) != 0) {
        SDL_free(conversion.buf);
        return 0;
    }
    if (result == 0) conversion.len_cvt = conversion.len;
    audio->sounds[index].data = conversion.buf;
    audio->sounds[index].length = (uint32_t)conversion.len_cvt;
    return 1;
}

static int audio_init(vcc_audio *audio)
{
    SDL_AudioSpec desired;
    unsigned index;
    SDL_zero(*audio);
    SDL_zero(desired);
    desired.freq = 11025;
    desired.format = AUDIO_U8;
    desired.channels = 1;
    desired.samples = 512;
    audio->device = SDL_OpenAudioDevice(NULL, 0, &desired, &audio->format, 0);
    if (audio->device == 0U) return 0;
    for (index = 1U; index < 11U; ++index) {
        if (!load_sound(audio, index)) return 0;
    }
    SDL_PauseAudioDevice(audio->device, 0);
    return 1;
}

static void audio_play(vcc_audio *audio, vcc_event event)
{
    unsigned index = (unsigned)event;
    if (audio->device != 0U && index < 11U && audio->sounds[index].data)
        (void)SDL_QueueAudio(audio->device, audio->sounds[index].data,
            audio->sounds[index].length);
}

static void audio_quit(vcc_audio *audio)
{
    unsigned index;
    if (audio->device != 0U) SDL_CloseAudioDevice(audio->device);
    for (index = 0U; index < 11U; ++index) SDL_free(audio->sounds[index].data);
}

static void tile_rect(unsigned tile, SDL_Rect *rect)
{
    unsigned sprite = vcc_tile_sprite(tile);
    rect->x = (int)(sprite / 16U) * SOURCE_TILE_SIZE;
    rect->y = (int)(sprite % 16U) * SOURCE_TILE_SIZE;
    rect->w = SOURCE_TILE_SIZE;
    rect->h = SOURCE_TILE_SIZE;
}

static void draw_background(SDL_Renderer *renderer, SDL_Texture *background)
{
    int x;
    int y;
    for (y = 0; y < SCREEN_HEIGHT; y += 196) {
        for (x = 0; x < SCREEN_WIDTH; x += 237) {
            SDL_Rect target = {x, y, 237, 196};
            (void)SDL_RenderCopy(renderer, background, NULL, &target);
        }
    }
}

static void draw_board(SDL_Renderer *renderer, const vcc_graphics *graphics,
    const vcc_game *game)
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
            uint8_t map_x = (uint8_t)(camera_x + x);
            uint8_t map_y = (uint8_t)(camera_y + y);
            uint8_t terrain = vcc_game_terrain_tile(game, map_x, map_y);
            uint8_t actor = vcc_game_actor_tile(game, map_x, map_y);
            SDL_Rect source;
            SDL_Rect target = {BOARD_X + x * BOARD_TILE_SIZE, BOARD_Y + y * BOARD_TILE_SIZE,
                BOARD_TILE_SIZE, BOARD_TILE_SIZE};
            tile_rect(terrain, &source);
            (void)SDL_RenderCopy(renderer, graphics->tiles, &source, &target);
            if (actor != 0U) {
                tile_rect(actor, &source);
                (void)SDL_RenderCopy(renderer, graphics->tiles, &source, &target);
            }
        }
    }
}

static int info_coordinate(int local)
{
    return local * INFO_SCALE_NUMERATOR / INFO_SCALE_DENOMINATOR;
}

static void draw_number(SDL_Renderer *renderer, SDL_Texture *digits,
    unsigned value, int x, int y, int yellow)
{
    unsigned divisor;
    if (value > 999U) value = 999U;
    for (divisor = 100U; divisor != 0U; divisor /= 10U) {
        unsigned digit = (value / divisor) % 10U;
        unsigned frame = digit + (yellow ? 0U : 12U);
        SDL_Rect source = {0, (int)frame * 23, 17, 23};
        SDL_Rect target = {x, y, info_coordinate(17), info_coordinate(23)};
        (void)SDL_RenderCopy(renderer, digits, &source, &target);
        x += info_coordinate(17);
    }
}

static void draw_inventory(SDL_Renderer *renderer, const vcc_graphics *graphics,
    const vcc_game *game)
{
    unsigned slot;
    for (slot = 0U; slot < 8U; ++slot) {
        SDL_Rect box = {INFO_X + info_coordinate(12 + (int)(slot % 4U) * 32),
            INFO_Y + info_coordinate(217 + (int)(slot / 4U) * 32),
            info_coordinate(32), info_coordinate(32)};
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        (void)SDL_RenderDrawLine(renderer, box.x, box.y, box.x + box.w - 1, box.y);
        (void)SDL_RenderDrawLine(renderer, box.x, box.y, box.x, box.y + box.h - 1);
        SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
        (void)SDL_RenderDrawLine(renderer, box.x + box.w - 1, box.y,
            box.x + box.w - 1, box.y + box.h - 1);
        (void)SDL_RenderDrawLine(renderer, box.x, box.y + box.h - 1,
            box.x + box.w - 1, box.y + box.h - 1);
        if ((slot < 4U && game->keys[slot] != 0U)
            || (slot >= 4U && game->boots[slot - 4U] != 0U)) {
            SDL_Rect source;
            unsigned tile = slot < 4U ? VCC_BLUE_KEY + slot : VCC_FLIPPERS + slot - 4U;
            tile_rect(tile, &source);
            (void)SDL_RenderCopy(renderer, graphics->tiles, &source, &box);
        }
    }
}

static void draw_info(SDL_Renderer *renderer, const vcc_graphics *graphics,
    const vcc_game *game)
{
    SDL_Rect target = {INFO_X, INFO_Y, info_coordinate(154), info_coordinate(300)};
    unsigned seconds = game->time_left_ticks == 0U ? 0U
        : (game->time_left_ticks + 19U) / 20U;
    (void)SDL_RenderCopy(renderer, graphics->info, NULL, &target);
    draw_number(renderer, graphics->digits, game->level->number,
        INFO_X + info_coordinate(44), INFO_Y + info_coordinate(34), 0);
    draw_number(renderer, graphics->digits, seconds,
        INFO_X + info_coordinate(44), INFO_Y + info_coordinate(99),
        game->level->time_limit == 0U || seconds <= 15U);
    draw_number(renderer, graphics->digits, game->chips_left,
        INFO_X + info_coordinate(44), INFO_Y + info_coordinate(189), 0);
    draw_inventory(renderer, graphics, game);
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

int main(void)
{
    uint8_t *bytes = NULL;
    size_t size = 0U;
    vcc_dat *dat = NULL;
    vcc_game game;
    vcc_graphics graphics = {0};
    vcc_audio audio = {0};
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_GameController *controller = NULL;
    uint16_t level_index = 0U;
    uint32_t next_tick;
    int running = 1;
    bytes = read_file("app0:/data/CHIPS.DAT", &size);
    dat = malloc(sizeof *dat);
    if (!bytes || !dat || vcc_dat_parse(dat, bytes, size) != VCC_DAT_OK
        || !vcc_game_start(&game, &dat->levels[0])) goto cleanup;
    free(bytes);
    bytes = NULL;
    (void)SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) < 0) goto cleanup;
    window = SDL_CreateWindow("Vita Chips Challenge", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : NULL;
    if (!renderer || SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT) != 0) goto cleanup;
    graphics.tiles = load_bmp(renderer, "app0:/data/OBJ32_4_RGB.bmp");
    graphics.actors = load_masked_tiles(renderer);
    graphics.background = load_bmp(renderer, "app0:/data/BACKGROUND_RGB.bmp");
    graphics.info = load_bmp(renderer, "app0:/data/INFOWND_RGB.bmp");
    graphics.digits = load_bmp(renderer, "app0:/data/200_RGB.bmp");
    if (!graphics.tiles || !graphics.actors || !graphics.background
        || !graphics.info || !graphics.digits) goto cleanup;
    if (!audio_init(&audio)) goto cleanup;
    if (SDL_NumJoysticks() > 0 && SDL_IsGameController(0)) controller = SDL_GameControllerOpen(0);
    next_tick = SDL_GetTicks() + 50U;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            vcc_direction direction = event_direction(&event);
            if (event.type == SDL_QUIT) running = 0;
            if (direction != VCC_DIR_NONE) (void)vcc_game_move(&game, direction);
            if ((event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_r)
                || (event.type == SDL_CONTROLLERBUTTONDOWN
                    && event.cbutton.button == SDL_CONTROLLER_BUTTON_X)) {
                if (game.status == VCC_WON && level_index + 1U < dat->level_count)
                    ++level_index;
                (void)vcc_game_start(&game, &dat->levels[level_index]);
            }
        }
        if ((int32_t)(SDL_GetTicks() - next_tick) >= 0) {
            vcc_game_tick(&game, VCC_DIR_NONE);
            next_tick += 50U;
        }
        audio_play(&audio, vcc_game_take_event(&game));
        SDL_SetRenderDrawColor(renderer, 0, 128, 0, 255);
        SDL_RenderClear(renderer);
        draw_background(renderer, graphics.background);
        draw_board(renderer, &graphics, &game);
        draw_info(renderer, &graphics, &game);
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }
cleanup:
    audio_quit(&audio);
    if (controller) SDL_GameControllerClose(controller);
    if (graphics.digits) SDL_DestroyTexture(graphics.digits);
    if (graphics.info) SDL_DestroyTexture(graphics.info);
    if (graphics.background) SDL_DestroyTexture(graphics.background);
    if (graphics.actors) SDL_DestroyTexture(graphics.actors);
    if (graphics.tiles) SDL_DestroyTexture(graphics.tiles);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    free(bytes);
    free(dat);
    return 0;
}
