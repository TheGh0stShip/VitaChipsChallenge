/* SPDX-License-Identifier: GPL-3.0-only */
/* Vita front end: the CHIPS.EXE main window, board, information panel,
 * menus, dialogs, sound, and music, laid out as the original lays out its
 * client area (2:0946) under a Windows 3.1 menu bar. */
#include "vcc/dat.h"
#include "vcc/game.h"
#include "vcc/music.h"
#include "vcc/progress.h"
#include "vcc/score.h"
#include "help.h"
#include "ui.h"
#include <SDL2/SDL.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 544
#define VIEW_TILES 9
#define TILE 32
#define CLIENT_Y UI_MENU_BAR_H
/* Board and panel origins from 2:0946 with DS:169E = DS:16A0 = 32 (5:0033,
 * screens taller than 350 lines). */
#define BOARD_X 32
#define BOARD_Y (CLIENT_Y + 32)
#define BOARD_SIZE (VIEW_TILES * TILE)
#define INFO_X (32 + 0x133)
#define INFO_Y (CLIENT_Y + 32 - 6)

#define SAVE_DIR "ux0:data/VitaChipsChallenge"
#define SAVE_PATH SAVE_DIR "/entpack.ini"
#define CAPTION "Chip's Challenge"  /* DS:0068 */
#define TICK_MS 110U                /* timer 1, 2:16FA */
#define REPEAT_DELAY_MS 250U        /* KeyboardDelay 0 (2:24D0) */
#define REPEAT_RATE_MS 33U
#define AUDIO_RATE 22050

/* Menu command identifiers from the CHIPSMENU resource. */
enum {
    CMD_ABOUT = 100, CMD_EXIT = 106, CMD_HELP_CONTENTS = 107, CMD_HELP_USE = 109,
    CMD_NEXT = 110, CMD_PREVIOUS = 111, CMD_RESTART = 113, CMD_NEW_GAME = 114,
    CMD_BEST_TIMES = 115, CMD_PAUSE = 116, CMD_MUSIC = 117, CMD_SOUND = 118,
    CMD_GOTO = 119, CMD_HELP_PLAY = 120, CMD_HELP_COMMANDS = 121, CMD_COLOR = 122
};

static const SDL_Color black = {0, 0, 0, 255};
static const SDL_Color red = {255, 0, 0, 255};
static const SDL_Color yellow = {255, 255, 0, 255};
static const SDL_Color cyan = {0, 255, 255, 255};
static const SDL_Color light = {192, 192, 192, 255};

/* ---- Files ---------------------------------------------------------- */

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
    data = malloc((size_t)length + 1U);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    data[length] = 0U;
    *size = (size_t)length;
    return data;
}

static SDL_Texture *load_bmp(SDL_Renderer *renderer, const char *path, int keyed)
{
    SDL_Surface *surface = SDL_LoadBMP(path);
    SDL_Texture *texture;
    if (!surface) return NULL;
    if (keyed) (void)SDL_SetColorKey(surface, SDL_TRUE, SDL_MapRGB(surface->format, 1U, 2U, 3U));
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

/* ---- Audio ---------------------------------------------------------- */

/* Default files from DS:040A, indexed by vcc_sound_id. CHIMES, HIT3, CLICK1,
 * and BELL are Windows system sounds absent from the game archive; with
 * SND_NODEFAULT (8:05AA) a missing file plays nothing. */
static const char *const sound_files[VCC_SOUND_COUNT] = {
    "BLIP2.WAV", "DOOR.WAV", "BUMMER.WAV", "DITTY1.WAV", NULL,
    "OOF3.WAV", "STRIKE.WAV", NULL, "CLICK3.WAV", "POP2.WAV", "WATER2.WAV",
    NULL, "TELEPORT.WAV", NULL, NULL
};

/* Music list (DS:04A6); missing files drop out of the rotation (8:0308). */
static const char *const music_files[] = {"CHIP01.MID", "CHIP02.MID", "CANYON.MID"};

typedef struct sample {
    int16_t *data;
    size_t length;
} sample;

typedef struct audio {
    SDL_AudioDeviceID device;
    sample sounds[VCC_SOUND_COUNT];
    const sample *playing;
    size_t position;
    vcc_music *music;
    uint8_t *songs[3];
    size_t song_sizes[3];
    int song_count;
} audio;

/* Performance counters written to the log once a second. */
static volatile Uint64 audio_busy_ticks;
static volatile Uint32 audio_calls;

static void audio_callback(void *userdata, Uint8 *stream, int length)
{
    Uint64 start = SDL_GetPerformanceCounter();
    audio *a = userdata;
    int16_t *out = (int16_t *)stream;
    size_t frames = (size_t)length / sizeof(int16_t);
    size_t index;
    if (a->music && vcc_music_playing(a->music)) vcc_music_render(a->music, out, frames);
    else memset(out, 0, (size_t)length);
    for (index = 0U; index < frames && a->playing; ++index) {
        int value = out[index] + a->playing->data[a->position++];
        out[index] = (int16_t)(value > 32767 ? 32767 : (value < -32768 ? -32768 : value));
        if (a->position >= a->playing->length) a->playing = NULL;
    }
    audio_busy_ticks += SDL_GetPerformanceCounter() - start;
    ++audio_calls;
}

static FILE *log_file;

static void log_open(void)
{
#ifdef __vita__
    extern int sceIoMkdir(const char *, int);
    (void)sceIoMkdir("ux0:data/VitaChipsChallenge", 0777);
#endif
    log_file = fopen("ux0:data/VitaChipsChallenge/log.txt", "w");
}

static void log_line(const char *format, ...)
{
    va_list args;
    if (!log_file) return;
    va_start(args, format);
    (void)vfprintf(log_file, format, args);
    va_end(args);
    (void)fputc('\n', log_file);
    (void)fflush(log_file);
}

static int load_sound(sample *out, const char *name)
{
    char path[64];
    SDL_AudioSpec source;
    uint8_t *data = NULL;
    uint32_t length = 0U;
    SDL_AudioCVT cvt;
    (void)snprintf(path, sizeof path, "app0:/data/%s", name);
    if (!SDL_LoadWAV(path, &source, &data, &length)) return 0;
    if (SDL_BuildAudioCVT(&cvt, source.format, source.channels, source.freq,
            AUDIO_S16SYS, 1, AUDIO_RATE) < 0) {
        SDL_FreeWAV(data);
        return 0;
    }
    cvt.len = (int)length;
    cvt.buf = SDL_malloc((size_t)cvt.len * (size_t)cvt.len_mult);
    if (!cvt.buf) { SDL_FreeWAV(data); return 0; }
    SDL_memcpy(cvt.buf, data, length);
    SDL_FreeWAV(data);
    if (cvt.needed && SDL_ConvertAudio(&cvt) != 0) { SDL_free(cvt.buf); return 0; }
    out->data = (int16_t *)cvt.buf;
    out->length = (size_t)(cvt.needed ? cvt.len_cvt : cvt.len) / sizeof(int16_t);
    return 1;
}

static int audio_init(audio *a)
{
    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    unsigned index;
    size_t bank_size = 0U;
    uint8_t *bank;
    SDL_zero(desired);
    desired.freq = AUDIO_RATE;
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = 1024;
    desired.callback = audio_callback;
    desired.userdata = a;
    for (index = 0U; index < VCC_SOUND_COUNT; ++index)
        if (sound_files[index]) (void)load_sound(&a->sounds[index], sound_files[index]);
    bank = read_file("app0:/data/GENMIDI.op2", &bank_size);
    if (bank) {
        a->music = vcc_music_create(bank, bank_size, AUDIO_RATE);
        free(bank);
    }
    for (index = 0U; index < 3U; ++index) {
        char path[64];
        (void)snprintf(path, sizeof path, "app0:/data/%s", music_files[index]);
        a->songs[a->song_count] = read_file(path, &a->song_sizes[a->song_count]);
        if (a->songs[a->song_count]) ++a->song_count;
    }
    a->device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (a->device == 0U) return 0;
    SDL_PauseAudioDevice(a->device, 0);
    return 1;
}

/* sndPlaySound at 8:056C: SND_ASYNC replaces the sound playing, while the
 * SND_NOSTOP form used for buttons pressed by monsters and blocks yields. */
static void audio_play(audio *a, vcc_sound_id id, int interrupt)
{
    const sample *s = &a->sounds[id];
    if (a->device == 0U || !s->data) return;
    SDL_LockAudioDevice(a->device);
    if (interrupt || !a->playing) {
        a->playing = s;
        a->position = 0U;
    }
    SDL_UnlockAudioDevice(a->device);
}

/* 8:0308: the level's song is file (level mod count), restarted. */
static void music_start(audio *a, int level)
{
    if (!a->music || a->song_count == 0 || a->device == 0U) return;
    SDL_LockAudioDevice(a->device);
    if (vcc_music_load(a->music, a->songs[level % a->song_count],
            a->song_sizes[level % a->song_count]))
        vcc_music_play(a->music);
    SDL_UnlockAudioDevice(a->device);
}

/* 8:02D4 */
static void music_stop(audio *a)
{
    if (!a->music || a->device == 0U) return;
    SDL_LockAudioDevice(a->device);
    vcc_music_stop(a->music);
    SDL_UnlockAudioDevice(a->device);
}

static void audio_quit(audio *a)
{
    unsigned index;
    if (a->device != 0U) SDL_CloseAudioDevice(a->device);
    for (index = 0U; index < VCC_SOUND_COUNT; ++index) SDL_free(a->sounds[index].data);
    for (index = 0U; index < 3U; ++index) free(a->songs[index]);
    vcc_music_destroy(a->music);
}

/* ---- Application state --------------------------------------------- */

typedef struct graphics {
    SDL_Texture *tiles;
    SDL_Texture *tiles_mono;
    SDL_Texture *actors;
    SDL_Texture *background;
    SDL_Texture *info;
    SDL_Texture *digits;
    SDL_Texture *chipend;
    SDL_Texture *banner;
    SDL_Texture *icon;
    SDL_Texture *board;  /* the ending draws here without clearing */
    int background_w;
    int background_h;
} graphics;

typedef struct app {
    SDL_Renderer *renderer;
    graphics gfx;
    audio sound;
    vcc_dat *dat;
    vcc_game game;
    vcc_progress progress;
    vcc_attempts attempts;
    uint16_t level_index;
    ui_menu_bar menu;
    ui_dialog *modal;
    int paused;          /* DS:0024 nesting count */
    int ending;          /* state+0xA36 */
    int quit;
} app;

static app the_app;

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

/* ---- Board and panel drawing --------------------------------------- */

static SDL_Texture *tile_sheet(const app *a)
{
    /* 5:0112 picks OBJ32_4 (VGA), OBJ32_4E (EGA), or OBJ32_1 (monochrome);
     * Options > Color off selects the monochrome set. */
    if (!a->progress.color && a->gfx.tiles_mono) return a->gfx.tiles_mono;
    return a->gfx.tiles;
}

static void tile_rect(unsigned tile, SDL_Rect *rect)
{
    rect->x = (int)(tile / 16U) * TILE;
    rect->y = (int)(tile % 16U) * TILE;
    rect->w = TILE;
    rect->h = TILE;
}

/* 2:00C4: a creature, Chip, or item over non-floor ground is drawn masked
 * over that ground; anything else draws only the top tile. */
static void draw_cell(const app *a, uint8_t top, uint8_t bottom, const SDL_Rect *target)
{
    SDL_Rect source;
    SDL_Texture *sheet = tile_sheet(a);
    if (bottom != VCC_FLOOR && top >= 0x40 && top <= 0x6F) {
        tile_rect(bottom, &source);
        (void)SDL_RenderCopy(a->renderer, sheet, &source, target);
        tile_rect(top, &source);
        (void)SDL_RenderCopy(a->renderer, a->progress.color ? a->gfx.actors : sheet,
            &source, target);
        return;
    }
    tile_rect(top, &source);
    (void)SDL_RenderCopy(a->renderer, sheet, &source, target);
}

/* 2:0DC6: the background bitmap tiled from the client origin, then the
 * board frame (2:1006 light gray, 2:0F06 raised bevel of four). */
static void draw_client(const app *a)
{
    int x;
    int y;
    SDL_Rect board = {BOARD_X, BOARD_Y, BOARD_SIZE, BOARD_SIZE};
    SDL_Rect ring = {BOARD_X - 2, BOARD_Y - 2, BOARD_SIZE + 4, BOARD_SIZE + 4};
    for (y = CLIENT_Y; y < UI_SCREEN_H; y += a->gfx.background_h)
        for (x = 0; x < UI_SCREEN_W; x += a->gfx.background_w) {
            SDL_Rect target = {x, y, a->gfx.background_w, a->gfx.background_h};
            (void)SDL_RenderCopy(a->renderer, a->gfx.background, NULL, &target);
        }
    ui_fill(a->renderer, ring, light);
    ui_fill(a->renderer, board, black);
    ui_bevel(a->renderer, ring, 4, 1);
}

static void camera(const vcc_game *game, int *cx, int *cy)
{
    *cx = game->chip_x - VIEW_TILES / 2;
    *cy = game->chip_y - VIEW_TILES / 2;
    if (*cx < 0) *cx = 0;
    if (*cy < 0) *cy = 0;
    if (*cx > 32 - VIEW_TILES) *cx = 32 - VIEW_TILES;
    if (*cy > 32 - VIEW_TILES) *cy = 32 - VIEW_TILES;
}

/* Title and password shown while the level waits for its first key
 * (2:1374): Arial bold from 16 points, yellow on black, in a raised box
 * one line above the bottom of the board. */
static void draw_level_title(const app *a)
{
    const vcc_level *level = a->game.level;
    char title[96] = "";
    char password[32] = "";
    int points;
    int has_title = level->title[0] != '\0';
    int has_password = level->password[0] != '\0';
    if (!has_title && !has_password) return;
    if (has_title) (void)snprintf(title, sizeof title, " %s ", level->title);
    if (has_password) (void)snprintf(password, sizeof password, " Password: %s ", level->password);
    for (points = 16; points >= 6; --points) {
        TTF_Font *font = ui_font(UI_FACE_ARIAL_BOLD, points, 0);
        int line_h;
        int width;
        int lines = has_title + has_password;
        int y;
        SDL_Rect box;
        if (!font) continue;
        width = ui_text_width(font, title);
        if (ui_text_width(font, password) > width) width = ui_text_width(font, password);
        if (width + 8 > BOARD_SIZE && points > 6) continue;
        line_h = TTF_FontHeight(font) * 2 / 3;
        y = BOARD_SIZE - line_h * (has_title && has_password ? 3 : 2);
        box.x = BOARD_X + (BOARD_SIZE - width) / 2;
        box.y = BOARD_Y + y;
        box.w = width;
        box.h = lines * line_h;
        ui_fill(a->renderer, box, black);
        if (has_title)
            ui_text(a->renderer, font, title,
                BOARD_X + (BOARD_SIZE - ui_text_width(font, title)) / 2, box.y, yellow);
        if (has_password)
            ui_text(a->renderer, font, password,
                BOARD_X + (BOARD_SIZE - ui_text_width(font, password)) / 2,
                box.y + (has_title ? line_h : 0), yellow);
        ui_bevel(a->renderer, box, 4, 1);
        return;
    }
}

static void draw_board(app *a)
{
    int cx;
    int cy;
    int x;
    int y;
    if (a->paused > 0) {
        /* 2:10DE: the paused board is black with PAUSED in red Arial. */
        TTF_Font *font = ui_font(UI_FACE_ARIAL, 32, 0);
        int w = ui_text_width(font, "PAUSED");
        int h = font ? TTF_FontHeight(font) * 2 / 3 : 0;
        ui_text(a->renderer, font, "PAUSED", BOARD_X + (BOARD_SIZE - w) / 2,
            BOARD_Y + (BOARD_SIZE - h) / 2, a->progress.color ? red : (SDL_Color){255, 255, 255, 255});
        return;
    }
    if (a->ending > 0) {
        SDL_Rect target = {BOARD_X, BOARD_Y, BOARD_SIZE, BOARD_SIZE};
        (void)SDL_RenderCopy(a->renderer, a->gfx.board, NULL, &target);
        return;
    }
    camera(&a->game, &cx, &cy);
    for (y = 0; y < VIEW_TILES; ++y)
        for (x = 0; x < VIEW_TILES; ++x) {
            SDL_Rect target = {BOARD_X + x * TILE, BOARD_Y + y * TILE, TILE, TILE};
            draw_cell(a, vcc_game_tile(&a->game, (uint8_t)(cx + x), (uint8_t)(cy + y), 0),
                vcc_game_tile(&a->game, (uint8_t)(cx + x), (uint8_t)(cy + y), 1), &target);
        }
    if (a->game.waiting) draw_level_title(a);
}

/* Counter window paint, 2:29A6 and 9:00EA. The digit sheet "200" is a
 * bottom-up DIB of 24 frames, 17x23 each; frame k counted from the bottom
 * is digit k for k <= 9, blank for 10, and '-' for 11. Frames 0-11 are the
 * green set and 12-23 the yellow set. Digits are centred in the 55x29
 * counter windows (2:0A3A) with leading zeros blanked. */
static void draw_counter(const app *a, int value, int dashes, int yellow_digits, int y)
{
    unsigned v = value < 0 ? 0U : (unsigned)value;
    unsigned digits[3];
    int index;
    if (dashes) {
        digits[0] = digits[1] = digits[2] = 11U;
    } else {
        digits[2] = v % 10U;
        digits[1] = (v % 100U) / 10U;
        digits[0] = v / 100U;
        if (digits[0] == 0U) {
            digits[0] = 10U;
            if (digits[1] == 0U) digits[1] = 10U;
        }
        if (digits[0] > 9U && digits[0] != 10U) digits[0] = 9U;
    }
    for (index = 0; index < 3; ++index) {
        unsigned frame = digits[index] + (yellow_digits ? 12U : 0U);
        SDL_Rect source = {0, (int)(23U - frame) * 23, 17, 23};
        SDL_Rect target = {INFO_X + 45 + 2 + index * 17, INFO_Y + y + 3, 17, 23};
        (void)SDL_RenderCopy(a->renderer, a->gfx.digits, &source, &target);
    }
}

/* Inventory window (2:0AC5) at (13, 221), keys above boots. */
static void draw_inventory(const app *a)
{
    unsigned slot;
    for (slot = 0U; slot < 8U; ++slot) {
        SDL_Rect box = {INFO_X + 13 + (int)(slot % 4U) * TILE,
            INFO_Y + 221 + (int)(slot / 4U) * TILE, TILE, TILE};
        SDL_Rect source;
        unsigned tile = VCC_FLOOR;
        if (slot < 4U && a->game.keys[slot] != 0) tile = VCC_BLUE_KEY + slot;
        if (slot >= 4U && a->game.boots[slot - 4U] != 0) tile = VCC_FLIPPERS + slot - 4U;
        tile_rect(tile, &source);
        (void)SDL_RenderCopy(a->renderer, tile_sheet(a), &source, &box);
    }
}

/* Hint window (2:0C1A, painted at 2:2BBE): replaces the chips counter and
 * inventory while Chip stands on a hint. Arial bold italic in cyan,
 * shrinking from 12 points to fit. */
static void draw_hint(const app *a)
{
    SDL_Rect frame = {INFO_X + 13 + 3, INFO_Y + 139 + 3, 128 - 6, 146 - 6};
    SDL_Rect text_rect = {frame.x + 1, frame.y + 1, frame.w - 2, frame.h - 2};
    char text[VCC_HINT_CAPACITY + 8];
    ui_bevel(a->renderer, frame, 3, 0);
    ui_fill(a->renderer, frame, black);
    (void)snprintf(text, sizeof text, "Hint: %s", a->game.level->hint);
    ui_draw_fitted(a->renderer, UI_FACE_ARIAL_BOLD, 1, 12, text, text_rect,
        a->progress.color ? cyan : (SDL_Color){255, 255, 255, 255});
}

/* Counter flags follow 2:0CBE: time is yellow at 15 seconds or less (so an
 * untimed level shows yellow dashes), chips are yellow once none remain. */
static void draw_info(const app *a)
{
    SDL_Rect target = {INFO_X, INFO_Y, 154, 300};
    (void)SDL_RenderCopy(a->renderer, a->gfx.info, NULL, &target);
    draw_counter(a, a->game.level->number, 0, 0, 34);
    draw_counter(a, a->game.time_left, a->game.level->time_limit == 0U,
        a->game.time_left <= 15, 96);
    if (a->game.bottom[(size_t)a->game.chip_y * 32U + (size_t)a->game.chip_x] == VCC_HINT
        && a->ending == 0) {
        draw_hint(a);
        return;
    }
    draw_counter(a, a->game.chips_left, 0, a->game.chips_left == 0, 186);
    draw_inventory(a);
}

static void render_scene(app *a)
{
    SDL_SetRenderDrawColor(a->renderer, 0, 0, 0, 255);
    SDL_RenderClear(a->renderer);
    draw_client(a);
    draw_board(a);
    draw_info(a);
    ui_menu_draw(a->renderer, &a->menu);
    if (a->modal) ui_dialog_draw(a->renderer, a->modal);
}

static void render(app *a)
{
    render_scene(a);
    SDL_RenderPresent(a->renderer);
}

static void help_backdrop_draw(void *context)
{
    render_scene(context);
}

/* ---- Modal dialogs -------------------------------------------------- */

/* A dialog procedure: returns the EndDialog result, or -1 to stay open. */
typedef int (*dialog_proc)(app *a, ui_dialog *dialog, const ui_notify *notify, void *data);

static int simple_proc(app *a, ui_dialog *dialog, const ui_notify *notify, void *data)
{
    (void)a;
    (void)dialog;
    (void)data;
    return notify->code == 0 ? notify->id : -1;
}

/* DialogBox: a nested message loop; the board keeps its state. */
static int run_dialog(app *a, ui_dialog *dialog, dialog_proc proc, void *data)
{
    ui_dialog *outer = a->modal;
    int result = -1;
    a->modal = dialog;
    while (result < 0 && !a->quit) {
        SDL_Event event;
        while (result < 0 && SDL_PollEvent(&event)) {
            ui_notify notify;
            if (event.type == SDL_QUIT) { a->quit = 1; break; }
            if (ui_dialog_event(dialog, &event, &notify))
                result = proc(a, dialog, &notify, data);
        }
        render(a);
        SDL_Delay(8);
    }
    SDL_StopTextInput();
    a->modal = outer;
    return result < 0 ? 0 : result;
}

/* 2:0000, the message box wrapper. */
static int message_box(app *a, const char *text, int yes_no, int icon)
{
    ui_dialog dialog;
    ui_message_box(&dialog, CAPTION, text, yes_no, icon);
    return run_dialog(a, &dialog, simple_proc, NULL);
}

/* 2:17DA and 2:1834: the nesting pause used around dialogs and Pause. */
static void pause_game(app *a) { ++a->paused; }
static void resume_game(app *a) { if (a->paused > 0) --a->paused; }

/* ---- Levels --------------------------------------------------------- */

static void set_menu_checks(app *a);

/* The parts of 4:0356 that follow its prompts. A new level is recorded as
 * visited with its password and raises "Highest Level"; a new level, not a
 * retry, starts its song (4:0441). */
static void load_level(app *a, uint16_t index, int retry)
{
    const vcc_level *level = &a->dat->levels[index];
    vcc_level_progress *entry = &a->progress.levels[level->number];
    a->level_index = index;
    a->ending = 0;
    if (!retry) vcc_attempts_new_level(&a->attempts);
    (void)vcc_game_start(&a->game, level);
    if (!retry && a->paused == 0 && a->progress.music) music_start(&a->sound, level->number);
    if (entry->password[0] == '\0')
        memcpy(entry->password, level->password, sizeof entry->password);
    a->progress.current_level = level->number;
    if (level->number > a->progress.highest_level) a->progress.highest_level = level->number;
    progress_save(&a->progress);
}

static int index_for_number(const app *a, int number)
{
    int index;
    for (index = 0; index < (int)a->dat->level_count; ++index)
        if (a->dat->levels[index].number == number) return index;
    return -1;
}

/* 4:0E48: a level is open without a password when it is level 1 or the
 * profile already holds its correct password. */
static int level_known(const app *a, int number)
{
    int index = index_for_number(a, number);
    if (number <= 1) return 1;
    if (index < 0 || number > (int)VCC_MAX_LEVELS) return 0;
    return SDL_strcasecmp(a->progress.levels[number].password,
        a->dat->levels[index].password) == 0;
}

typedef struct password_data {
    int number;
    const char *password;
} password_data;

/* 4:1016, DLG_PASSWORD. */
static int password_proc(app *a, ui_dialog *dialog, const ui_notify *notify, void *data)
{
    password_data *p = data;
    char message[UI_TEXT_CAPACITY + 48];
    ui_control *edit = ui_find(dialog, 101);
    if (notify->code != 0) return -1;
    if (notify->id == 2) return 0;
    if (notify->id != 1 || !edit) return -1;
    if (SDL_strcasecmp(edit->text, p->password) == 0) return 1;
    if (edit->text[0]) (void)snprintf(message, sizeof message,
        "Sorry, \"%s\" is not the correct password.", edit->text);
    else (void)snprintf(message, sizeof message, "You must enter a password.");
    (void)message_box(a, message, 0, 2);
    ui_focus(dialog, 101);
    return -1;
}

/* 4:115C: changing to a level whose password is not known asks for it. */
static int password_gate(app *a, int number)
{
    ui_dialog dialog;
    password_data data;
    char prompt[64];
    ui_control *control;
    int index = index_for_number(a, number);
    int result;
    if (number == a->game.level->number || level_known(a, number)) return 1;
    if (index < 0) return 1;
    pause_game(a);
    data.number = number;
    data.password = a->dat->levels[index].password;
    ui_dialog_begin(&dialog, "Password Entry", ui_dlu(0, 0, 181, 0).w, ui_dlu(0, 0, 0, 56).h);
    (void)snprintf(prompt, sizeof prompt, "Please enter the password for level %d:", number);
    (void)ui_add(&dialog, UI_STATIC, 100, ui_dlu(9, 11, 132, 8), prompt);
    control = ui_add(&dialog, UI_EDIT, 101, ui_dlu(142, 9, 27, 12), "");
    if (control) control->max_length = 9;
    control = ui_add(&dialog, UI_BUTTON, 1, ui_dlu(42, 36, 40, 14), "OK");
    if (control) control->is_default = 1;
    (void)ui_add(&dialog, UI_BUTTON, 2, ui_dlu(98, 36, 40, 14), "Cancel");
    dialog.cancel_id = 2;
    ui_focus(&dialog, 101);
    result = run_dialog(a, &dialog, password_proc, &data);
    resume_game(a);
    return result;
}

static void go_to_level(app *a, int number)
{
    int index = index_for_number(a, number);
    if (index >= 0) load_level(a, (uint16_t)index, 0);
}

static void next_level(app *a)
{
    if (a->level_index + 1U < a->dat->level_count)
        load_level(a, (uint16_t)(a->level_index + 1U), 0);
}

/* 4:0356 with retry set. */
static void retry_level(app *a)
{
    if (vcc_attempts_restart(&a->attempts, a->game.level->number, (uint16_t)a->game.moves)) {
        int answer = message_box(a,
            "You seem to be having trouble with this level.\n"
            "Would you like to skip to the next level?", 1, 1);
        vcc_attempts_answer(&a->attempts, answer == 6);
        if (answer == 6) {
            next_level(a);
            return;
        }
    }
    load_level(a, a->level_index, 1);
}

/* ---- Engine hooks --------------------------------------------------- */

static void hook_sound(void *context, vcc_sound_id id, int interrupt)
{
    app *a = context;
    if (a->progress.sounds) audio_play(&a->sound, id, interrupt);
}

/* 2:0B9A, then the retry load. */
static void hook_died(void *context, vcc_game *game)
{
    static const char *const messages[] = {
        "",
        "Ooops! Don't step in the fire without fire boots!",
        "Ooops! Chip can't swim without flippers!",
        "Ooops! Don't touch the bombs!",
        "Ooops! Watch out for moving blocks!",
        "Ooops! Look out for creatures!",
        "Ooops! Out of time!"
    };
    app *a = context;
    int reason = game->death;
    if (reason < 1 || reason > 6) reason = 5;
    (void)message_box(a, messages[reason], 0, 0);
    retry_level(a);
}

/* DLG_COMPLETE (template at file offset 0x40400), filled as in 6:0422. */
static void show_complete(app *a)
{
    static const char *const titles[] = {
        "Yowser! First Try!", "Go Bit Buster!",
        "Finished! Good Work!", "At last! You did it!"
    };
    const vcc_level *level = a->game.level;
    vcc_level_progress *entry = &a->progress.levels[level->number];
    vcc_completion c;
    ui_dialog dialog;
    char text[UI_TEXT_CAPACITY];
    ui_control *button;
    int index;
    vcc_score_completion(&c, level->number, a->game.time_left,
        a->attempts.attempts, a->progress.highest_level, &entry->record,
        a->progress.current_score);
    entry->record = c.saved;
    a->progress.current_score = c.total_score;
    progress_save(&a->progress);

    ui_dialog_begin(&dialog, "Level Complete!", ui_dlu(0, 0, 136, 0).w, ui_dlu(0, 0, 0, 119).h);
    (void)ui_add(&dialog, UI_STATIC, 101, ui_dlu(9, 7, 117, 8), titles[c.title]);
    (void)snprintf(text, sizeof text, "Time Bonus:  %d", (int)c.time_bonus);
    (void)ui_add(&dialog, UI_STATIC, 102, ui_dlu(9, 21, 117, 8), text);
    (void)snprintf(text, sizeof text, "Level Bonus:  %ld", (long)c.level_bonus);
    (void)ui_add(&dialog, UI_STATIC, 103, ui_dlu(9, 35, 117, 8), text);
    (void)snprintf(text, sizeof text, "Level Score:  %ld", (long)c.level_score);
    (void)ui_add(&dialog, UI_STATIC, 108, ui_dlu(9, 49, 117, 8), text);
    (void)snprintf(text, sizeof text, "Total Score:  %ld", (long)c.total_score);
    (void)ui_add(&dialog, UI_STATIC, 104, ui_dlu(9, 63, 117, 8), text);
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
    (void)ui_add(&dialog, UI_STATIC, 105, ui_dlu(9, 77, 117, 19), text);
    for (index = 0; index < dialog.count; ++index) dialog.controls[index].align = UI_CENTER;
    button = ui_add(&dialog, UI_BUTTON, 106, ui_dlu(48, 99, 40, 14), "Onward!");
    if (button) button->is_default = 1;
    (void)run_dialog(a, &dialog, simple_proc, NULL);
}

/* Interludes after levels 50, 60, ..., 140 (table at DS:0EB8). */
static const char *const interludes[] = {
    "Picking up chips is what the challenge is all about. But on the ice, Chip gets chapped and feels like a chump instead of a champ.",
    "Chip hits the ice and decides to chill out. Then he runs into a fake wall and turns the maze into a thrash-a-thon!",
    "Chip is halfway through the world's hardest puzzle. If he succeeds, maybe the kids will stop calling him computer breath!",
    "Chip used to spend his time programming computer games and making models. But that was just practice for this brain-buster!",
    "'I can do it! I know I can!' Chip thinks as the going gets tougher. Besides, Melinda the Mental Marvel waits at the end!",
    "Besides being an angel on earth, Melinda is the top scorer in the Challenge--and the president of the Bit Busters.",
    "Chip can't wait to join the Bit Busters! The club's already figured out the school's password and accessed everyone's grades!",
    "If Chip's grades aren't as good as Melinda's, maybe she'll come over to his house and help him study!",
    "'I've made it this far,' Chip thinks. 'Totally fair, with my mega-brain.' Then he starts the next maze. 'Totally unfair!' he yelps.",
    "Groov-u-loids! Chip makes it almost to the end. He's stoked!"
};

static void begin_board_drawing(app *a)
{
    (void)SDL_SetRenderTarget(a->renderer, a->gfx.board);
    (void)SDL_RenderSetScale(a->renderer, 1.0f, 1.0f);
}

static void end_board_drawing(app *a)
{
    (void)SDL_SetRenderTarget(a->renderer, NULL);
    (void)SDL_RenderSetScale(a->renderer, 1.5f, 1.5f);
}

static void capture_board(app *a)
{
    int cx;
    int cy;
    int x;
    int y;
    begin_board_drawing(a);
    camera(&a->game, &cx, &cy);
    for (y = 0; y < VIEW_TILES; ++y)
        for (x = 0; x < VIEW_TILES; ++x) {
            SDL_Rect target = {x * TILE, y * TILE, TILE, TILE};
            draw_cell(a, vcc_game_tile(&a->game, (uint8_t)(cx + x), (uint8_t)(cy + y), 0),
                vcc_game_tile(&a->game, (uint8_t)(cx + x), (uint8_t)(cy + y), 1), &target);
        }
    end_board_drawing(a);
}

/* 7:0CCA */
static void hook_completed(void *context, vcc_game *game)
{
    app *a = context;
    int number = game->level->number;
    show_complete(a);
    if (number == 144 || number == 149) {
        /* 7:0D72: start the ending; the timer drives 7:0A74. */
        capture_board(a);
        a->ending = 1;
        a->game.sliding = 0;
        a->game.death = 0;
        a->game.slip_count = 0U;
        a->game.monster_count = 0U;
        return;
    }
    if (number % 10 == 0 && number / 10 >= 5 && number / 10 <= 14)
        (void)message_box(a, interludes[number / 10 - 5], 0, 0);
    next_level(a);
}

static void show_final_messages(app *a)
{
    char text[600];
    int completed = 0;
    int level;
    (void)message_box(a, "Great Job, Chip!\nYou did it!  You finished the challenge!", 0, 0);
    /* LoadBitmap("chipend") over the board (7:0B86). */
    begin_board_drawing(a);
    (void)SDL_RenderCopy(a->renderer, a->gfx.chipend, NULL, NULL);
    end_board_drawing(a);
    a->ending = 0x69;
    (void)message_box(a, "Melinda herself offers Chip membership in the exclusive Bit Busters "
        "computer club, and gives him access to the club's computer system.  "
        "Chip is in heaven!", 0, 0);
    for (level = 1; level <= 149; ++level)
        if (a->progress.levels[level].record.present) ++completed;
    (void)snprintf(text, sizeof text, "You completed %d levels, and your total score for the "
        "challenge is %li points.\n\nYou can still improve your score, by completing levels "
        "that you skipped, and getting better times on each level.  When you replay a level, "
        "if your new score is better than your old, your score will be adjusted by the "
        "difference.  Select Best Times from the Game menu to see your scores for each level.",
        completed, (long)a->progress.current_score);
    (void)message_box(a, text, 0, 0);
}

/* 7:0A74: the ending, one step per board timer tick. Frames draw over the
 * previous ones without clearing, as the original paints the board DC. */
static void ending_tick(app *a)
{
    int count = a->ending;
    if (count >= 0x69) return;
    begin_board_drawing(a);
    if (count < 32) {
        static const uint8_t exits[3] = {VCC_EXIT, VCC_EXIT_ANIM_1, VCC_EXIT_ANIM_2};
        int grow = count * 8;
        int cx;
        int cy;
        SDL_Rect target;
        camera(&a->game, &cx, &cy);
        target.x = (a->game.chip_x - cx) * TILE - grow / 2;
        target.y = (a->game.chip_y - cy) * TILE - grow / 2;
        if (target.x < 0) target.x = 0;
        if (target.y < 0) target.y = 0;
        target.w = grow + TILE;
        target.h = grow + TILE;
        draw_cell(a, VCC_CHIP_S, exits[count % 3], &target);
    } else if (count < 0x68 && count % 2 == 0) {
        /* 3:072E(2) chooses Chip facing south or the cheering tile 0x39. */
        SDL_Rect target = {0, 0, BOARD_SIZE, BOARD_SIZE};
        unsigned r;
        a->game.random_seed = a->game.random_seed * UINT32_C(0x343FD) + UINT32_C(0x269EC3);
        r = (unsigned)((a->game.random_seed >> 16) & 0x7FFFU) % 2U;
        draw_cell(a, r == 0U ? VCC_CHIP_S : 0x39, VCC_EXIT, &target);
    }
    end_board_drawing(a);
    if (count == 0x68) {
        show_final_messages(a);
        return;
    }
    ++a->ending;
}

/* ---- Menus and commands -------------------------------------------- */

static void build_menu(app *a)
{
    ui_menu *m;
    memset(&a->menu, 0, sizeof a->menu);
    a->menu.active = -1;
    a->menu.count = 4;
    m = &a->menu.menus[0];
    m->label = "&Game";
    m->items[0] = (ui_menu_item){"&New Game\tF2", CMD_NEW_GAME, 0, 0};
    m->items[1] = (ui_menu_item){"&Pause\tF3", CMD_PAUSE, 0, 0};
    m->items[2] = (ui_menu_item){"Best &Times...", CMD_BEST_TIMES, 0, 0};
    m->items[3] = (ui_menu_item){NULL, 0, 0, 0};
    m->items[4] = (ui_menu_item){"E&xit", CMD_EXIT, 0, 0};
    m->count = 5;
    m = &a->menu.menus[1];
    m->label = "&Options";
    m->items[0] = (ui_menu_item){"&Background Music", CMD_MUSIC, 0, 0};
    m->items[1] = (ui_menu_item){"&Sound Effects", CMD_SOUND, 0, 0};
    m->items[2] = (ui_menu_item){"&Color", CMD_COLOR, 0, 0};
    m->count = 3;
    m = &a->menu.menus[2];
    m->label = "&Level";
    m->items[0] = (ui_menu_item){"&Restart\tCtrl+R", CMD_RESTART, 0, 0};
    m->items[1] = (ui_menu_item){"&Next\tCtrl+N", CMD_NEXT, 0, 0};
    m->items[2] = (ui_menu_item){"&Previous\tCtrl+P", CMD_PREVIOUS, 0, 0};
    m->items[3] = (ui_menu_item){"&Go To...", CMD_GOTO, 0, 0};
    m->count = 4;
    m = &a->menu.menus[3];
    m->label = "&Help";
    m->items[0] = (ui_menu_item){"&Contents\tF1", CMD_HELP_CONTENTS, 0, 0};
    m->items[1] = (ui_menu_item){"&How to Play", CMD_HELP_PLAY, 0, 0};
    m->items[2] = (ui_menu_item){"C&ommands", CMD_HELP_COMMANDS, 0, 0};
    m->items[3] = (ui_menu_item){"How to &Use Help", CMD_HELP_USE, 0, 0};
    m->items[4] = (ui_menu_item){NULL, 0, 0, 0};
    m->items[5] = (ui_menu_item){"&About Chip's Challenge...", CMD_ABOUT, 0, 0};
    m->count = 6;
    set_menu_checks(a);
}

/* WM_CREATE (2:2334) checks the options from the profile and greys Music
 * when no MIDI output exists. */
static void set_menu_checks(app *a)
{
    a->menu.menus[0].items[1].checked = a->paused > 0;
    a->menu.menus[1].items[0].checked = a->progress.music != 0;
    a->menu.menus[1].items[1].checked = a->progress.sounds != 0;
    a->menu.menus[1].items[2].checked = a->progress.color != 0;
    a->menu.menus[1].items[0].disabled = a->sound.song_count == 0 || !a->sound.music;
}

typedef struct goto_data {
    int number;
} goto_data;

/* 6:0000, DLG_GOTO, with the password check of 4:0EAA. */
static int goto_proc(app *a, ui_dialog *dialog, const ui_notify *notify, void *data)
{
    goto_data *g = data;
    ui_control *number_edit = ui_find(dialog, 100);
    ui_control *password_edit = ui_find(dialog, 101);
    char *end = NULL;
    long number;
    int valid;
    if (notify->code != 0) return -1;
    if (notify->id == 2) return 0;
    if (notify->id != 1 || !number_edit || !password_edit) return -1;
    number = strtol(number_edit->text, &end, 10);
    valid = number_edit->text[0] != '\0' && end && *end == '\0';
    if (!valid && number_edit->text[0] == '\0') { valid = 1; number = 0; }
    if (!valid || number < 0 || number > (long)a->dat->level_count) {
        (void)message_box(a, "That is not a valid level number.", 0, 3);
        ui_focus(dialog, 100);
        return -1;
    }
    if (password_edit->text[0] == '\0' && number == 0) {
        (void)message_box(a, "You must enter a level and/or password.", 0, 3);
        ui_focus(dialog, 100);
        return -1;
    }
    if (number != 0) {
        int index = index_for_number(a, (int)number);
        if (number == 1 || a->progress.levels[number].password[0] != '\0'
            || (index >= 0 && SDL_strcasecmp(a->dat->levels[index].password,
                password_edit->text) == 0)) {
            g->number = (int)number;
            return 1;
        }
    } else {
        int index;
        for (index = 0; index < (int)a->dat->level_count; ++index)
            if (SDL_strcasecmp(a->dat->levels[index].password, password_edit->text) == 0) {
                g->number = a->dat->levels[index].number;
                return 1;
            }
    }
    (void)message_box(a, "You must enter a valid password.", 0, 3);
    ui_focus(dialog, 101);
    return -1;
}

static void command_goto(app *a)
{
    ui_dialog dialog;
    goto_data data = {0};
    ui_control *control;
    int result;
    pause_game(a);
    ui_dialog_begin(&dialog, "Go To Level", ui_dlu(0, 0, 151, 0).w, ui_dlu(0, 0, 0, 94).h);
    (void)ui_add(&dialog, UI_STATIC, -1, ui_dlu(9, 7, 132, 19),
        "Enter a level number and password, or just a password.");
    control = ui_add(&dialog, UI_EDIT, 100, ui_dlu(69, 30, 32, 12), "");
    if (control) control->max_length = 3;
    control = ui_add(&dialog, UI_EDIT, 101, ui_dlu(69, 46, 32, 12), "");
    if (control) control->max_length = 9;
    control = ui_add(&dialog, UI_STATIC, -1, ui_dlu(16, 32, 51, 8), "Level number:");
    if (control) control->align = UI_RIGHT;
    control = ui_add(&dialog, UI_STATIC, -1, ui_dlu(16, 47, 51, 8), "Password:");
    if (control) control->align = UI_RIGHT;
    control = ui_add(&dialog, UI_BUTTON, 1, ui_dlu(27, 74, 40, 14), "OK");
    if (control) control->is_default = 1;
    (void)ui_add(&dialog, UI_BUTTON, 2, ui_dlu(83, 74, 40, 14), "Cancel");
    dialog.cancel_id = 2;
    ui_focus(&dialog, 100);
    result = run_dialog(a, &dialog, goto_proc, &data);
    resume_game(a);
    if (result == 1 && data.number != a->game.level->number) go_to_level(a, data.number);
}

typedef struct times_data {
    char items[150][64];
} times_data;

/* 6:018E, DLG_BESTTIMES. */
static int times_proc(app *a, ui_dialog *dialog, const ui_notify *notify, void *data)
{
    ui_control *list = ui_find(dialog, 100);
    (void)data;
    if (notify->id == 100) {
        ui_control *go = ui_find(dialog, 101);
        if (notify->code == 1 && go) go->disabled = 0;
        if (notify->code != 2) return -1;
    } else if (notify->id == 1) {
        return 1;
    } else if (notify->id == 2) {
        return 0;
    } else if (notify->id != 101) {
        return -1;
    }
    if (a->progress.highest_level < 1U || !list || list->item_count == 0 || list->selection < 0)
        return -1;
    {
        int number = list->selection + 1;
        if (number != a->game.level->number && !password_gate(a, number)) return -1;
        return 1000 + number;
    }
}

static void command_best_times(app *a)
{
    static times_data data;
    ui_dialog dialog;
    ui_control *list;
    ui_control *button;
    char text[64];
    int completed = 0;
    int level;
    int highest = a->progress.highest_level;
    int result;
    pause_game(a);
    ui_dialog_begin(&dialog, "Best Times", ui_dlu(0, 0, 159, 0).w, ui_dlu(0, 0, 0, 159).h);
    list = ui_add(&dialog, UI_LIST, 100, ui_dlu(7, 49, 144, 81), "");
    (void)ui_add(&dialog, UI_STATIC, -1, ui_dlu(7, 36, 144, 8),
        "Level number, seconds left, level score:");
    button = ui_add(&dialog, UI_BUTTON, 1, ui_dlu(31, 138, 40, 14), "OK");
    if (button) button->is_default = 1;
    button = ui_add(&dialog, UI_BUTTON, 101, ui_dlu(87, 138, 40, 14), "Go To");
    if (button) button->disabled = 1;
    if (highest <= 0) {
        (void)snprintf(data.items[0], sizeof data.items[0], "No levels completed.");
        list->item_count = 1;
    } else {
        for (level = 1; level <= highest && level < 150; ++level) {
            const vcc_level_record *r = &a->progress.levels[level].record;
            if (r->present) {
                (void)snprintf(data.items[level - 1], sizeof data.items[0],
                    "Level %d:  %d seconds, %li points", level, r->seconds, (long)r->score);
                ++completed;
            } else {
                (void)snprintf(data.items[level - 1], sizeof data.items[0],
                    "Level %d:  not completed", level);
            }
        }
        list->item_count = highest < 150 ? highest : 149;
    }
    list->items = data.items;
    list->selection = -1;
    (void)snprintf(text, sizeof text, "You have completed %d level%s.", completed,
        completed == 1 ? "" : "s");
    (void)ui_add(&dialog, UI_STATIC, 102, ui_dlu(7, 10, 142, 8), text);
    (void)snprintf(text, sizeof text, "Your total score is %li points.",
        (long)a->progress.current_score);
    (void)ui_add(&dialog, UI_STATIC, 103, ui_dlu(7, 23, 142, 8), text);
    dialog.cancel_id = 2;
    ui_focus(&dialog, 100);
    result = run_dialog(a, &dialog, times_proc, &data);
    resume_game(a);
    if (result > 1000) go_to_level(a, result - 1000);
}

/* WEP4UTIL WEPABOUT2: template 101 in the System font with the
 * Entertainment Pack banner (control 301), the application icon, name,
 * credits (string 258), and the DLL's copyright (string 259). */
static void command_about(app *a)
{
    ui_dialog dialog;
    ui_control *control;
    SDL_Rect icon = ui_dlu_system(10, 60, 0, 0);
    int width = ui_dlu_system(0, 0, 140, 0).w;
    pause_game(a);
    ui_dialog_begin(&dialog, "About Chip's Challenge", width, ui_dlu_system(0, 0, 0, 148).h);
    dialog.face = UI_FACE_SYSTEM;
    control = ui_add(&dialog, UI_IMAGE, 301, (SDL_Rect){(width - 260) / 2, 10, 259, 64}, "");
    if (control) {
        control->image = a->gfx.banner;
        control->image_source = (SDL_Rect){0, 0, 259, 64};
    }
    control = ui_add(&dialog, UI_IMAGE, 701, (SDL_Rect){icon.x, icon.y, 32, 32}, "");
    if (control) {
        control->image = a->gfx.icon;
        control->image_source = (SDL_Rect){0, 0, 32, 32};
    }
    control = ui_add(&dialog, UI_STATIC, 702, ui_dlu_system(26, 63, 92, 16), "Chip's Challenge");
    if (control) control->align = UI_CENTER;
    control = ui_add(&dialog, UI_STATIC, 703, ui_dlu_system(10, 82, 124, 16),
        "By Tony Krueger\nArtwork by Ed Halley");
    if (control) control->align = UI_CENTER;
    control = ui_add(&dialog, UI_STATIC, 704, ui_dlu_system(20, 102, 100, 16),
        "Copyright \xa9 1992 Microsoft Corp.\nAll rights reserved.");
    if (control) control->align = UI_CENTER;
    control = ui_add(&dialog, UI_BUTTON, 1, ui_dlu_system(53, 124, 34, 15), "OK");
    if (control) control->is_default = 1;
    dialog.cancel_id = 1;
    (void)run_dialog(a, &dialog, simple_proc, NULL);
    resume_game(a);
}

/* WEPHELP with HELP_KEY "Contents", "How To Play", or "Commands"
 * (DS:0248, DS:0252, DS:025E). HELP_HELPONHELP needs WINHELP.HLP, which
 * the archive does not include, so it opens the contents. */
static void command_help(app *a, int id)
{
    const char *keyword = "Contents";
    if (id == CMD_HELP_PLAY) keyword = "How To Play";
    if (id == CMD_HELP_COMMANDS) keyword = "Commands";
    pause_game(a);
    help_run(a->renderer, keyword, help_backdrop_draw, a);
    resume_game(a);
}

static void command(app *a, int id)
{
    switch (id) {
    case CMD_NEW_GAME:
        /* 2:1F9E */
        if (a->progress.highest_level != 1U && message_box(a,
                "Starting a new game will begin you back at level 1, reset your score to "
                "zero, and forget the passwords to any levels you have visited.", 1, 1) != 6)
            break;
        vcc_progress_new_game(&a->progress);
        go_to_level(a, 1);
        break;
    case CMD_PAUSE:
        /* 2:2014: Pause stops the music; resuming restarts the level's song. */
        if (a->paused > 0) {
            resume_game(a);
            if (a->progress.music && a->paused == 0) music_start(&a->sound, a->game.level->number);
        } else {
            music_stop(&a->sound);
            pause_game(a);
        }
        break;
    case CMD_BEST_TIMES: command_best_times(a); break;
    case CMD_EXIT: a->quit = 1; break;
    case CMD_MUSIC:
        a->progress.music = (int16_t)!a->progress.music;
        if (a->progress.music) music_start(&a->sound, a->game.level->number);
        else music_stop(&a->sound);
        progress_save(&a->progress);
        break;
    case CMD_SOUND:
        a->progress.sounds = (int16_t)!a->progress.sounds;
        progress_save(&a->progress);
        if (a->progress.sounds) audio_play(&a->sound, VCC_SOUND_SOUND_ON, 1);
        break;
    case CMD_COLOR:
        a->progress.color = (int16_t)!a->progress.color;
        progress_save(&a->progress);
        break;
    case CMD_RESTART: retry_level(a); break;
    case CMD_NEXT: {
        int number = a->game.level->number + 1;
        if (index_for_number(a, number) >= 0 && password_gate(a, number)) go_to_level(a, number);
        break;
    }
    case CMD_PREVIOUS: {
        int number = a->game.level->number - 1;
        if (number >= 1 && password_gate(a, number)) go_to_level(a, number);
        break;
    }
    case CMD_GOTO: command_goto(a); break;
    case CMD_ABOUT: command_about(a); break;
    case CMD_HELP_CONTENTS: case CMD_HELP_PLAY: case CMD_HELP_COMMANDS: case CMD_HELP_USE:
        command_help(a, id);
        break;
    default:
        break;
    }
    set_menu_checks(a);
}

/* CHIPSMENU accelerators, plus Vita buttons for the same commands. */
static int accelerator(const SDL_Event *event)
{
    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        int ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
        if (key == SDLK_F1) return CMD_HELP_CONTENTS;
        if (key == SDLK_F2) return CMD_NEW_GAME;
        if (key == SDLK_F3) return CMD_PAUSE;
        if (ctrl && key == SDLK_r) return CMD_RESTART;
        if (ctrl && key == SDLK_n) return CMD_NEXT;
        if (ctrl && key == SDLK_p) return CMD_PREVIOUS;
    }
    if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        switch (event->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_BACK: return CMD_PAUSE;
        case SDL_CONTROLLER_BUTTON_Y: return CMD_RESTART;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return CMD_PREVIOUS;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return CMD_NEXT;
        default: break;
        }
    }
    return 0;
}

static vcc_direction button_direction(Uint8 button)
{
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return VCC_DIR_NORTH;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return VCC_DIR_WEST;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return VCC_DIR_SOUTH;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return VCC_DIR_EAST;
    default: return VCC_DIR_NONE;
    }
}

static vcc_direction key_direction(SDL_Keycode key)
{
    switch (key) {
    case SDLK_UP: return VCC_DIR_NORTH;
    case SDLK_LEFT: return VCC_DIR_WEST;
    case SDLK_DOWN: return VCC_DIR_SOUTH;
    case SDLK_RIGHT: return VCC_DIR_EAST;
    default: return VCC_DIR_NONE;
    }
}

/* ---- Main ----------------------------------------------------------- */

static int load_graphics(app *a)
{
    int access;
    Uint32 format;
    graphics *g = &a->gfx;
    g->tiles = load_bmp(a->renderer, "app0:/data/OBJ32_4_RGB.bmp", 0);
    g->tiles_mono = load_bmp(a->renderer, "app0:/data/OBJ32_1_RGB.bmp", 0);
    g->actors = load_bmp(a->renderer, "app0:/data/OBJ32_MASKED.bmp", 1);
    g->background = load_bmp(a->renderer, "app0:/data/BACKGROUND_RGB.bmp", 0);
    g->info = load_bmp(a->renderer, "app0:/data/INFOWND_RGB.bmp", 0);
    g->digits = load_bmp(a->renderer, "app0:/data/200_RGB.bmp", 0);
    g->chipend = load_bmp(a->renderer, "app0:/data/CHIPEND_RGB.bmp", 0);
    g->banner = load_bmp(a->renderer, "app0:/data/WEP_666_RGB.bmp", 0);
    g->icon = load_bmp(a->renderer, "app0:/data/ICON_RGB.bmp", 1);
    g->board = SDL_CreateTexture(a->renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, BOARD_SIZE, BOARD_SIZE);
    if (!g->tiles || !g->actors || !g->background || !g->info || !g->digits || !g->board)
        return 0;
    (void)SDL_QueryTexture(g->background, &format, &access, &g->background_w, &g->background_h);
    return 1;
}

static void free_graphics(graphics *g)
{
    SDL_Texture *all[] = {g->tiles, g->tiles_mono, g->actors, g->background,
        g->info, g->digits, g->chipend, g->banner, g->icon, g->board};
    size_t index;
    for (index = 0U; index < sizeof all / sizeof all[0]; ++index)
        if (all[index]) SDL_DestroyTexture(all[index]);
}

int main(void)
{
    app *a = &the_app;
    uint8_t *bytes = NULL;
    size_t size = 0U;
    SDL_Window *window = NULL;
    SDL_GameController *controller = NULL;
    vcc_direction held = VCC_DIR_NONE;
    uint32_t next_repeat = 0U;
    uint32_t next_tick;
    int index;
    bytes = read_file("app0:/data/CHIPS.DAT", &size);
    a->dat = malloc(sizeof *a->dat);
    if (!bytes || !a->dat || vcc_dat_parse(a->dat, bytes, size) != VCC_DAT_OK
        || a->dat->level_count == 0U) goto cleanup;
    free(bytes);
    bytes = NULL;
    (void)SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) < 0) goto cleanup;
    if (TTF_Init() != 0) goto cleanup;
    window = SDL_CreateWindow("Vita Chips Challenge", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    a->renderer = window ? SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE) : NULL;
    if (!a->renderer || SDL_RenderSetScale(a->renderer, 1.5f, 1.5f) != 0) goto cleanup;
    if (!load_graphics(a)) goto cleanup;
    if (!ui_init("app0:/data/fonts/LiberationSans-Regular.ttf",
            "app0:/data/fonts/LiberationSans-Bold.ttf")) goto cleanup;
    log_open();
    log_line("start: audio %d, songs %d, music %s", audio_init(&a->sound),
        a->sound.song_count, a->sound.music ? "yes" : "no");
    (void)help_load(a->renderer, "app0:/data/help");
    if (SDL_NumJoysticks() > 0 && SDL_IsGameController(0)) controller = SDL_GameControllerOpen(0);

    a->game.hooks.context = a;
    a->game.hooks.sound = hook_sound;
    a->game.hooks.died = hook_died;
    a->game.hooks.completed = hook_completed;
    vcc_game_seed(&a->game, SDL_GetTicks());  /* srand(GetCurrentTime()), 2:092E */
    /* Startup resumes at "Current Level" with "Current Score" (2:0B24). */
    progress_load(&a->progress);
    build_menu(a);
    index = index_for_number(a, a->progress.current_level);
    load_level(a, (uint16_t)(index < 0 ? 0 : index), 0);

#ifdef VCC_PREVIEW
    if (VCC_PREVIEW == 1) { a->game.death = VCC_DEATH_FIRE; hook_died(a, &a->game); }
    if (VCC_PREVIEW == 3) { a->attempts.attempts = 2; show_complete(a); }
    if (VCC_PREVIEW == 4) command_about(a);
    if (VCC_PREVIEW == 5) command_best_times(a);
    if (VCC_PREVIEW == 6) command_goto(a);
    if (VCC_PREVIEW == 7) { a->menu.active = 2; a->menu.open = 1; a->menu.item = 1; }
    if (VCC_PREVIEW == 8) { capture_board(a); a->ending = 30; }
    if (VCC_PREVIEW == 9) pause_game(a);
    if (VCC_PREVIEW == 10) command_help(a, CMD_HELP_PLAY);
#endif
    next_tick = SDL_GetTicks() + TICK_MS;
    {
    uint32_t log_time = SDL_GetTicks() + 1000U;
    uint32_t frames = 0U;
    Uint64 frame_ticks = 0U;
    while (!a->quit) {
        SDL_Event event;
        uint32_t now;
        Uint64 frame_start = SDL_GetPerformanceCounter();
        while (SDL_PollEvent(&event)) {
            int id;
            if (event.type == SDL_QUIT) a->quit = 1;
            id = ui_menu_event(&a->menu, &event);
            if (id) { command(a, id); continue; }
            if (a->menu.active >= 0) continue;
            id = accelerator(&event);
            if (id) { command(a, id); continue; }
            if (event.type == SDL_CONTROLLERBUTTONUP
                && button_direction(event.cbutton.button) == held)
                held = VCC_DIR_NONE;
            if (a->ending > 0 || a->paused > 0) continue;
            if (event.type == SDL_KEYDOWN) {
                /* Windows key repeat drives held keys (2:2540). */
                vcc_game_key(&a->game, key_direction(event.key.keysym.sym));
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                vcc_direction direction = button_direction(event.cbutton.button);
                vcc_game_key(&a->game, direction);
                if (direction != VCC_DIR_NONE) {
                    held = direction;
                    next_repeat = SDL_GetTicks() + REPEAT_DELAY_MS;
                }
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                /* Board WM_LBUTTONDOWN (2:27EA): walk toward the cell. */
                int x = event.button.x - BOARD_X;
                int y = event.button.y - BOARD_Y;
                if (x >= 0 && y >= 0 && x < BOARD_SIZE && y < BOARD_SIZE) {
                    int cx;
                    int cy;
                    camera(&a->game, &cx, &cy);
                    vcc_game_click(&a->game, cx + x / TILE, cy + y / TILE);
                }
            }
        }
        now = SDL_GetTicks();
        if (held != VCC_DIR_NONE && a->paused == 0 && a->menu.active < 0 && a->ending == 0
            && (int32_t)(now - next_repeat) >= 0) {
            vcc_game_key(&a->game, held);
            next_repeat = now + REPEAT_RATE_MS;
        }
        if ((a->game.waiting && a->ending == 0) || a->paused > 0) {
            next_tick = now + TICK_MS;
        } else if ((int32_t)(now - next_tick) >= 0) {
            next_tick += TICK_MS;
            if (a->ending > 0) ending_tick(a);
            else vcc_game_tick(&a->game);
            now = SDL_GetTicks();
            if ((int32_t)(now - next_tick) > 0) next_tick = now + TICK_MS;
        }
        render(a);
        frame_ticks += SDL_GetPerformanceCounter() - frame_start;
        ++frames;
        if ((int32_t)(SDL_GetTicks() - log_time) >= 0) {
            double frequency = (double)SDL_GetPerformanceFrequency();
            log_line("level %d fps %u frame %.2f ms audio %.1f%% (%u calls) music %d sounds %d",
                a->game.level->number, frames, frames ? (double)frame_ticks * 1000.0 / frequency / frames : 0.0,
                (double)audio_busy_ticks * 100.0 / frequency, audio_calls, a->progress.music,
                a->progress.sounds);
            frames = 0U;
            frame_ticks = 0U;
            audio_busy_ticks = 0U;
            audio_calls = 0U;
            log_time += 1000U;
        }
        SDL_Delay(4);
    }
    }
cleanup:
    if (log_file) fclose(log_file);
    if (a->dat && a->game.level) progress_save(&a->progress);
    audio_quit(&a->sound);
    help_free();
    ui_quit();
    if (controller) SDL_GameControllerClose(controller);
    free_graphics(&a->gfx);
    if (a->renderer) SDL_DestroyRenderer(a->renderer);
    if (window) SDL_DestroyWindow(window);
    if (TTF_WasInit()) TTF_Quit();
    SDL_Quit();
    free(bytes);
    free(a->dat);
    return 0;
}
