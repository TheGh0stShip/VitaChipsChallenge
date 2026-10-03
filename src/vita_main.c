/* SPDX-License-Identifier: GPL-3.0-only */
#include "vcc/dat.h"
#include "vcc/game.h"
#include "vcc/progress.h"
#include "vcc/score.h"
#include "ui.h"
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 544
#define LOGICAL_WIDTH 640
#define LOGICAL_HEIGHT 363
#define VIEW_TILES 9
#define SOURCE_TILE_SIZE 32
#define BOARD_TILE_SIZE 32
#define BOARD_X 32
#define BOARD_Y 34
#define INFO_X 340
#define INFO_Y 31

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
    vcc_sound sounds[VCC_EVENT_COUNT];
} vcc_audio;

/* Default files from DS:040A, indexed by vcc_event. CHIMES, HIT3, CLICK1,
 * and BELL are Windows system sounds absent from the game archive; with
 * SND_NODEFAULT (8:05AA) a missing file plays nothing. */
static const char *const sound_files[VCC_EVENT_COUNT] = {
    NULL, "BLIP2.WAV", "DOOR.WAV", "BUMMER.WAV", "DITTY1.WAV", NULL,
    "OOF3.WAV", "STRIKE.WAV", NULL, "CLICK3.WAV", "POP2.WAV", "WATER2.WAV",
    NULL, "TELEPORT.WAV", NULL, NULL
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
    for (index = 1U; index < VCC_EVENT_COUNT; ++index) {
        if (!load_sound(audio, index)) return 0;
    }
    SDL_PauseAudioDevice(audio->device, 0);
    return 1;
}

/* sndPlaySound(SND_ASYNC) at 8:05AD: a new sound replaces the one playing. */
static void audio_play(vcc_audio *audio, vcc_event event)
{
    unsigned index = (unsigned)event;
    if (audio->device != 0U && index < VCC_EVENT_COUNT && audio->sounds[index].data) {
        SDL_ClearQueuedAudio(audio->device);
        (void)SDL_QueueAudio(audio->device, audio->sounds[index].data,
            audio->sounds[index].length);
    }
}

static void audio_quit(vcc_audio *audio)
{
    unsigned index;
    if (audio->device != 0U) SDL_CloseAudioDevice(audio->device);
    for (index = 0U; index < VCC_EVENT_COUNT; ++index) SDL_free(audio->sounds[index].data);
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
    for (y = 0; y < LOGICAL_HEIGHT; y += 196) {
        for (x = 0; x < LOGICAL_WIDTH; x += 237) {
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
    {
        SDL_Rect surround = {BOARD_X - 6, BOARD_Y - 6,
            VIEW_TILES * BOARD_TILE_SIZE + 12, VIEW_TILES * BOARD_TILE_SIZE + 12};
        SDL_SetRenderDrawColor(renderer, 192, 192, 192, 255);
        (void)SDL_RenderFillRect(renderer, &surround);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        (void)SDL_RenderDrawLine(renderer, surround.x, surround.y,
            surround.x + surround.w - 1, surround.y);
        (void)SDL_RenderDrawLine(renderer, surround.x, surround.y,
            surround.x, surround.y + surround.h - 1);
        SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
        (void)SDL_RenderDrawLine(renderer, surround.x + surround.w - 1, surround.y,
            surround.x + surround.w - 1, surround.y + surround.h - 1);
        (void)SDL_RenderDrawLine(renderer, surround.x, surround.y + surround.h - 1,
            surround.x + surround.w - 1, surround.y + surround.h - 1);
    }
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

/* Counter window paint, 2:29A6 and 9:00EA. The digit sheet "200" is a
 * bottom-up DIB of 24 frames, 17x23 each; frame k counted from the bottom
 * is digit k for k <= 9, blank for 10, and '-' for 11. Frames 0-11 are the
 * green set and 12-23 the yellow set. Leading zeros are blanked, and the
 * "dashes" flag draws "---" (used for untimed levels). */
#define DIGIT_BLANK 10U
#define DIGIT_DASH 11U

static void draw_digit(SDL_Renderer *renderer, SDL_Texture *digits,
    unsigned frame, int yellow, int x, int y)
{
    unsigned bottom_index = frame + (yellow ? 12U : 0U);
    SDL_Rect source = {0, (int)(23U - bottom_index) * 23, 17, 23};
    SDL_Rect target = {x, y, 17, 23};
    (void)SDL_RenderCopy(renderer, digits, &source, &target);
}

static void draw_counter(SDL_Renderer *renderer, SDL_Texture *digits,
    unsigned value, int dashes, int yellow, int x, int y)
{
    unsigned hundreds;
    unsigned tens;
    unsigned ones;
    if (dashes) {
        hundreds = tens = ones = DIGIT_DASH;
    } else {
        ones = value % 10U;
        tens = (value % 100U) / 10U;
        hundreds = value / 100U;
        if (hundreds == 0U) {
            hundreds = DIGIT_BLANK;
            if (tens == 0U) tens = DIGIT_BLANK;
        }
        if (hundreds > 9U && hundreds != DIGIT_BLANK) hundreds = 9U;
    }
    draw_digit(renderer, digits, hundreds, yellow, x, y);
    draw_digit(renderer, digits, tens, yellow, x + 17, y);
    draw_digit(renderer, digits, ones, yellow, x + 34, y);
}

static void draw_inventory(SDL_Renderer *renderer, const vcc_graphics *graphics,
    const vcc_game *game)
{
    unsigned slot;
    for (slot = 0U; slot < 8U; ++slot) {
        SDL_Rect box = {INFO_X + 12 + (int)(slot % 4U) * 32,
            INFO_Y + 217 + (int)(slot / 4U) * 32, 32, 32};
        SDL_Rect source;
        unsigned tile = VCC_FLOOR;
        if (slot < 4U && game->keys[slot] != 0U) tile = VCC_BLUE_KEY + slot;
        if (slot >= 4U && game->boots[slot - 4U] != 0U) tile = VCC_FLIPPERS + slot - 4U;
        tile_rect(tile, &source);
        (void)SDL_RenderCopy(renderer, graphics->tiles, &source, &box);
    }
}

static unsigned seconds_left(const vcc_game *game)
{
    return game->time_left_ticks == 0U ? 0U : (game->time_left_ticks + 19U) / 20U;
}

/* Counter flags follow 2:0CBE: time is yellow at 15 seconds or less (so an
 * untimed level shows yellow dashes), chips are yellow once none remain. */
static void draw_info(SDL_Renderer *renderer, const vcc_graphics *graphics,
    const vcc_game *game)
{
    SDL_Rect target = {INFO_X, INFO_Y, 154, 300};
    unsigned seconds = seconds_left(game);
    (void)SDL_RenderCopy(renderer, graphics->info, NULL, &target);
    draw_counter(renderer, graphics->digits, game->level->number, 0, 0,
        INFO_X + 47, INFO_Y + 37);
    draw_counter(renderer, graphics->digits, seconds,
        game->level->time_limit == 0U, seconds <= 15U, INFO_X + 47, INFO_Y + 99);
    draw_counter(renderer, graphics->digits, game->chips_left, 0,
        game->chips_left == 0U, INFO_X + 47, INFO_Y + 189);
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

/* ---- Session flow --------------------------------------------------- */

#define SAVE_DIR "ux0:data/VitaChipsChallenge"
#define SAVE_PATH SAVE_DIR "/entpack.ini"
#define CAPTION "Chip's Challenge"  /* DS:0068 */

typedef enum dialog_kind {
    DIALOG_NONE,
    DIALOG_DEATH,
    DIALOG_TROUBLE,
    DIALOG_COMPLETE,
    DIALOG_FINISHED
} dialog_kind;

typedef struct session {
    vcc_dat *dat;
    vcc_game game;
    vcc_progress progress;
    vcc_attempts attempts;
    uint16_t level_index;
    ui_dialog dialog;
    dialog_kind kind;
    int outcome_shown;  /* the current win or death already has its dialog */
} session;

static void progress_load(vcc_progress *progress)
{
    size_t size = 0U;
    uint8_t *text = read_file(SAVE_PATH, &size);
    if (text) {
        vcc_progress_parse(progress, (const char *)text, size);
        free(text);
    } else {
        vcc_progress_reset(progress);
    }
}

static void progress_save(const vcc_progress *progress)
{
    static char text[16384];
    size_t length = vcc_progress_format(progress, text, sizeof text);
    SDL_RWops *file;
    if (length >= sizeof text) return;
#ifdef __vita__
    {
        extern int sceIoMkdir(const char *, int);
        (void)sceIoMkdir(SAVE_DIR, 0777);
    }
#endif
    file = SDL_RWFromFile(SAVE_PATH, "wb");
    if (!file) return;
    (void)SDL_RWwrite(file, text, 1, length);
    (void)SDL_RWclose(file);
}

/* Starts a level. A different level clears the attempt counters and is
 * recorded as visited, with its password, in the profile. */
static void session_load(session *s, uint16_t index, int same_level)
{
    const vcc_level *level = &s->dat->levels[index];
    vcc_level_progress *entry = &s->progress.levels[level->number];
    s->level_index = index;
    s->outcome_shown = 0;
    if (!same_level) vcc_attempts_new_level(&s->attempts);
    (void)vcc_game_start(&s->game, level);
    if (entry->password[0] == '\0')
        memcpy(entry->password, level->password, sizeof entry->password);
    s->progress.current_level = level->number;
    if (level->number > s->progress.highest_level)
        s->progress.highest_level = level->number;
    progress_save(&s->progress);
}

static const char *death_message(vcc_status status)
{
    /* Table at 2:0BC8 indexed by death reason state+0x816. */
    switch (status) {
    case VCC_BURNED: return "Ooops! Don't step in the fire without fire boots!";
    case VCC_DROWNED: return "Ooops! Chip can't swim without flippers!";
    case VCC_BOMBED: return "Ooops! Don't touch the bombs!";
    case VCC_SQUASHED: return "Ooops! Watch out for moving blocks!";
    case VCC_COLLIDED: return "Ooops! Look out for creatures!";
    case VCC_TIMEOUT: return "Ooops! Out of time!";
    default: return "";
    }
}

static void open_message(session *s, const ui_fonts *fonts, dialog_kind kind,
    const char *text, int yes_no, int question)
{
    ui_message_box(&s->dialog, fonts, CAPTION, text, yes_no, question);
    s->kind = kind;
}

/* DLG_COMPLETE (template at file offset 0x40400), filled as in 6:0422. */
static void open_complete(session *s)
{
    static const char *const titles[] = {
        "Yowser! First Try!", "Go Bit Buster!",
        "Finished! Good Work!", "At last! You did it!"
    };
    const vcc_level *level = s->game.level;
    vcc_level_progress *entry = &s->progress.levels[level->number];
    vcc_completion c;
    char text[UI_TEXT_CAPACITY];
    vcc_score_completion(&c, level->number, (int16_t)seconds_left(&s->game),
        s->attempts.attempts, s->progress.highest_level, &entry->record,
        s->progress.current_score);
    entry->record = c.saved;
    s->progress.current_score = c.total_score;
    progress_save(&s->progress);

    ui_dialog_begin(&s->dialog, "Level Complete!", 136 * 6 / 4, 119 * 13 / 8);
    ui_dialog_static(&s->dialog, ui_dlu(9, 7, 117, 8), UI_CENTER, titles[c.title]);
    (void)snprintf(text, sizeof text, "Time Bonus:  %d", (int)c.time_bonus);
    ui_dialog_static(&s->dialog, ui_dlu(9, 21, 117, 8), UI_CENTER, text);
    (void)snprintf(text, sizeof text, "Level Bonus:  %ld", (long)c.level_bonus);
    ui_dialog_static(&s->dialog, ui_dlu(9, 35, 117, 8), UI_CENTER, text);
    (void)snprintf(text, sizeof text, "Level Score:  %ld", (long)c.level_score);
    ui_dialog_static(&s->dialog, ui_dlu(9, 49, 117, 8), UI_CENTER, text);
    (void)snprintf(text, sizeof text, "Total Score:  %ld", (long)c.total_score);
    ui_dialog_static(&s->dialog, ui_dlu(9, 63, 117, 8), UI_CENTER, text);
    switch (c.message) {
    case VCC_RECORD_ESTABLISHED:
        (void)snprintf(text, sizeof text, "You have established a time record for this level!");
        break;
    case VCC_RECORD_BEAT_TIME:
        (void)snprintf(text, sizeof text, "You beat the previous time record by %ld second%s!",
            (long)c.message_delta, c.message_delta > 1 ? "s" : "");
        break;
    case VCC_RECORD_MORE_POINTS:
        (void)snprintf(text, sizeof text, "You increased your score on this level by %ld point%s!",
            (long)c.message_delta, c.message_delta > 1 ? "s" : "");
        break;
    default:
        text[0] = '\0';
        break;
    }
    ui_dialog_static(&s->dialog, ui_dlu(9, 77, 117, 19), UI_CENTER, text);
    ui_dialog_button(&s->dialog, ui_dlu(48, 99, 40, 14), 106, "Onward!", 1);
    s->dialog.cancel_id = 0;
    s->kind = DIALOG_COMPLETE;
}

static void restart_level(session *s, const ui_fonts *fonts)
{
    if (vcc_attempts_restart(&s->attempts, s->game.level->number, s->game.moves)) {
        open_message(s, fonts, DIALOG_TROUBLE,
            "You seem to be having trouble with this level.\n"
            "Would you like to skip to the next level?", 1, 1);
        return;
    }
    session_load(s, s->level_index, 1);
}

static void next_level(session *s, const ui_fonts *fonts)
{
    if (s->level_index + 1U < s->dat->level_count) {
        session_load(s, (uint16_t)(s->level_index + 1U), 0);
    } else {
        open_message(s, fonts, DIALOG_FINISHED,
            "Great Job, Chip!\nYou did it!  You finished the challenge!", 0, 0);
    }
}

static void dialog_result(session *s, const ui_fonts *fonts, int id)
{
    dialog_kind kind = s->kind;
    s->dialog.open = 0;
    s->kind = DIALOG_NONE;
    switch (kind) {
    case DIALOG_DEATH:
        restart_level(s, fonts);
        break;
    case DIALOG_TROUBLE:
        vcc_attempts_answer(&s->attempts, id == 6);
        if (id == 6) next_level(s, fonts);
        else session_load(s, s->level_index, 1);
        break;
    case DIALOG_COMPLETE:
        next_level(s, fonts);
        break;
    default:
        break;
    }
}

static uint16_t level_index_for(const vcc_dat *dat, uint16_t number)
{
    uint16_t index;
    for (index = 0U; index < dat->level_count; ++index)
        if (dat->levels[index].number == number) return index;
    return 0U;
}

int main(void)
{
    uint8_t *bytes = NULL;
    size_t size = 0U;
    static session s;
    vcc_graphics graphics = {0};
    vcc_audio audio = {0};
    ui_fonts fonts = {0};
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_GameController *controller = NULL;
    uint32_t next_tick;
    int running = 1;
    bytes = read_file("app0:/data/CHIPS.DAT", &size);
    s.dat = malloc(sizeof *s.dat);
    if (!bytes || !s.dat || vcc_dat_parse(s.dat, bytes, size) != VCC_DAT_OK
        || s.dat->level_count == 0U) goto cleanup;
    free(bytes);
    bytes = NULL;
    (void)SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) < 0) goto cleanup;
    if (TTF_Init() != 0) goto cleanup;
    window = SDL_CreateWindow("Vita Chips Challenge", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : NULL;
    if (!renderer || SDL_RenderSetScale(renderer, 1.5f, 1.5f) != 0) goto cleanup;
    graphics.tiles = load_bmp(renderer, "app0:/data/OBJ32_4_RGB.bmp");
    graphics.actors = load_masked_tiles(renderer);
    graphics.background = load_bmp(renderer, "app0:/data/BACKGROUND_RGB.bmp");
    graphics.info = load_bmp(renderer, "app0:/data/INFOWND_RGB.bmp");
    graphics.digits = load_bmp(renderer, "app0:/data/200_RGB.bmp");
    if (!graphics.tiles || !graphics.actors || !graphics.background
        || !graphics.info || !graphics.digits) goto cleanup;
    if (!ui_fonts_open(&fonts, "app0:/data/fonts/LiberationSans-Regular.ttf",
            "app0:/data/fonts/LiberationSans-Bold.ttf")) goto cleanup;
    if (!audio_init(&audio)) goto cleanup;
    if (SDL_NumJoysticks() > 0 && SDL_IsGameController(0)) controller = SDL_GameControllerOpen(0);

    /* Startup resumes at "Current Level" with "Current Score" (2:0B24). */
    progress_load(&s.progress);
    session_load(&s, level_index_for(s.dat, s.progress.current_level), 0);

#ifdef VCC_PREVIEW
    /* Emulator capture hook: 1 death box, 2 trouble prompt, 3 completion. */
    if (VCC_PREVIEW == 1) open_message(&s, &fonts, DIALOG_DEATH, death_message(VCC_BURNED), 0, 0);
    if (VCC_PREVIEW == 2) open_message(&s, &fonts, DIALOG_TROUBLE,
        "You seem to be having trouble with this level.\n"
        "Would you like to skip to the next level?", 1, 1);
    if (VCC_PREVIEW == 3) { s.attempts.attempts = 2; open_complete(&s); }
    s.outcome_shown = 1;
#endif
    next_tick = SDL_GetTicks() + 50U;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;
            if (s.dialog.open) {
                int id = ui_dialog_event(&s.dialog, &event);
                if (id != 0) dialog_result(&s, &fonts, id);
                continue;
            }
            {
                vcc_direction direction = event_direction(&event);
                if (direction != VCC_DIR_NONE) (void)vcc_game_move(&s.game, direction);
            }
            if ((event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_r
                    && (SDL_GetModState() & KMOD_CTRL))
                || (event.type == SDL_CONTROLLERBUTTONDOWN
                    && event.cbutton.button == SDL_CONTROLLER_BUTTON_Y)) {
                /* Level > Restart (Ctrl+R) counts as an attempt. */
                restart_level(&s, &fonts);
            }
        }
        if (s.dialog.open) {
            /* Modal dialogs suspend the game timer (2:17A2 / 2:17BA). */
            next_tick = SDL_GetTicks() + 50U;
        } else if ((int32_t)(SDL_GetTicks() - next_tick) >= 0) {
            vcc_game_tick(&s.game, VCC_DIR_NONE);
            next_tick += 50U;
        }
        audio_play(&audio, vcc_game_take_event(&s.game));
        if (!s.dialog.open && !s.outcome_shown && s.game.status != VCC_PLAYING) {
            s.outcome_shown = 1;
            if (s.game.status == VCC_WON)
                open_complete(&s);
            else
                open_message(&s, &fonts, DIALOG_DEATH, death_message(s.game.status), 0, 0);
        }
        SDL_SetRenderDrawColor(renderer, 0, 128, 0, 255);
        SDL_RenderClear(renderer);
        draw_background(renderer, graphics.background);
        draw_board(renderer, &graphics, &s.game);
        draw_info(renderer, &graphics, &s.game);
        ui_dialog_draw(renderer, &fonts, &s.dialog);
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }
cleanup:
    audio_quit(&audio);
    ui_fonts_close(&fonts);
    if (controller) SDL_GameControllerClose(controller);
    if (graphics.digits) SDL_DestroyTexture(graphics.digits);
    if (graphics.info) SDL_DestroyTexture(graphics.info);
    if (graphics.background) SDL_DestroyTexture(graphics.background);
    if (graphics.actors) SDL_DestroyTexture(graphics.actors);
    if (graphics.tiles) SDL_DestroyTexture(graphics.tiles);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (TTF_WasInit()) TTF_Quit();
    SDL_Quit();
    free(bytes);
    free(s.dat);
    return 0;
}
