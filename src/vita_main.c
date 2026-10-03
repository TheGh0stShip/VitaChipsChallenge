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
    vcc_sound sounds[VCC_SOUND_COUNT];
} vcc_audio;

/* Default files from DS:040A, indexed by vcc_sound_id. CHIMES, HIT3, CLICK1,
 * and BELL are Windows system sounds absent from the game archive; with
 * SND_NODEFAULT (8:05AA) a missing file plays nothing. */
static const char *const sound_files[VCC_SOUND_COUNT] = {
    "BLIP2.WAV", "DOOR.WAV", "BUMMER.WAV", "DITTY1.WAV", NULL,
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
    for (index = 0U; index < VCC_SOUND_COUNT; ++index) {
        if (!load_sound(audio, index)) return 0;
    }
    SDL_PauseAudioDevice(audio->device, 0);
    return 1;
}

/* sndPlaySound at 8:056C: SND_ASYNC replaces the sound playing, while the
 * SND_NOSTOP form used for buttons pressed by monsters and blocks yields. */
static void audio_play(vcc_audio *audio, vcc_sound_id id, int interrupt)
{
    unsigned index = (unsigned)id;
    if (audio->device != 0U && index < VCC_SOUND_COUNT && audio->sounds[index].data) {
        if (!interrupt && SDL_GetQueuedAudioSize(audio->device) != 0U) return;
        SDL_ClearQueuedAudio(audio->device);
        (void)SDL_QueueAudio(audio->device, audio->sounds[index].data,
            audio->sounds[index].length);
    }
}

static void audio_quit(vcc_audio *audio)
{
    unsigned index;
    if (audio->device != 0U) SDL_CloseAudioDevice(audio->device);
    for (index = 0U; index < VCC_SOUND_COUNT; ++index) SDL_free(audio->sounds[index].data);
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

/* ---- Presentation --------------------------------------------------- */

/* 2:00C4: a creature, Chip, or item over non-floor ground is drawn masked
 * over that ground; anything else draws only the top tile. */
static void draw_cell(SDL_Renderer *renderer, const vcc_graphics *graphics,
    uint8_t top, uint8_t bottom, SDL_Rect *target)
{
    SDL_Rect source;
    if (bottom != VCC_FLOOR && top >= 0x40 && top <= 0x6F) {
        tile_rect(bottom, &source);
        (void)SDL_RenderCopy(renderer, graphics->tiles, &source, target);
        tile_rect(top, &source);
        (void)SDL_RenderCopy(renderer, graphics->actors, &source, target);
        return;
    }
    tile_rect(top, &source);
    (void)SDL_RenderCopy(renderer, graphics->tiles, &source, target);
}

static void bevel(SDL_Renderer *renderer, SDL_Rect r, int depth, int raised)
{
    int i;
    for (i = 0; i < depth; ++i) {
        SDL_SetRenderDrawColor(renderer, raised ? 255 : 128, raised ? 255 : 128,
            raised ? 255 : 128, 255);
        (void)SDL_RenderDrawLine(renderer, r.x + i, r.y + i, r.x + r.w - 1 - i, r.y + i);
        (void)SDL_RenderDrawLine(renderer, r.x + i, r.y + i, r.x + i, r.y + r.h - 1 - i);
        SDL_SetRenderDrawColor(renderer, raised ? 128 : 255, raised ? 128 : 255,
            raised ? 128 : 255, 255);
        (void)SDL_RenderDrawLine(renderer, r.x + r.w - 1 - i, r.y + i,
            r.x + r.w - 1 - i, r.y + r.h - 1 - i);
        (void)SDL_RenderDrawLine(renderer, r.x + i, r.y + r.h - 1 - i,
            r.x + r.w - 1 - i, r.y + r.h - 1 - i);
    }
}

/* Viewport (4:04DC, 2:056E): nine tiles centred on Chip, clamped. */
static void draw_board(SDL_Renderer *renderer, const vcc_graphics *graphics,
    const vcc_game *game)
{
    int camera_x = game->chip_x - VIEW_TILES / 2;
    int camera_y = game->chip_y - VIEW_TILES / 2;
    int x;
    int y;
    SDL_Rect surround = {BOARD_X - 6, BOARD_Y - 6,
        VIEW_TILES * BOARD_TILE_SIZE + 12, VIEW_TILES * BOARD_TILE_SIZE + 12};
    if (camera_x < 0) camera_x = 0;
    if (camera_y < 0) camera_y = 0;
    if (camera_x > (int)VCC_MAP_WIDTH - VIEW_TILES) camera_x = (int)VCC_MAP_WIDTH - VIEW_TILES;
    if (camera_y > (int)VCC_MAP_HEIGHT - VIEW_TILES) camera_y = (int)VCC_MAP_HEIGHT - VIEW_TILES;
    SDL_SetRenderDrawColor(renderer, 192, 192, 192, 255);
    (void)SDL_RenderFillRect(renderer, &surround);
    bevel(renderer, surround, 1, 1);
    for (y = 0; y < VIEW_TILES; ++y) {
        for (x = 0; x < VIEW_TILES; ++x) {
            uint8_t mx = (uint8_t)(camera_x + x);
            uint8_t my = (uint8_t)(camera_y + y);
            SDL_Rect target = {BOARD_X + x * BOARD_TILE_SIZE, BOARD_Y + y * BOARD_TILE_SIZE,
                BOARD_TILE_SIZE, BOARD_TILE_SIZE};
            draw_cell(renderer, graphics, vcc_game_tile(game, mx, my, 0),
                vcc_game_tile(game, mx, my, 1), &target);
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
    int value, int dashes, int yellow, int x, int y)
{
    unsigned v = value < 0 ? 0U : (unsigned)value;
    unsigned hundreds;
    unsigned tens;
    unsigned ones;
    if (dashes) {
        hundreds = tens = ones = DIGIT_DASH;
    } else {
        ones = v % 10U;
        tens = (v % 100U) / 10U;
        hundreds = v / 100U;
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
        if (slot < 4U && game->keys[slot] != 0) tile = VCC_BLUE_KEY + slot;
        if (slot >= 4U && game->boots[slot - 4U] != 0) tile = VCC_FLIPPERS + slot - 4U;
        tile_rect(tile, &source);
        (void)SDL_RenderCopy(renderer, graphics->tiles, &source, &box);
    }
}

/* Hint window (2:0C1A, painted at 2:2BBE): replaces the chips counter and
 * inventory while Chip stands on a hint. */
static void draw_hint(SDL_Renderer *renderer, const vcc_game *game)
{
    SDL_Rect frame = {INFO_X + 13, INFO_Y + 139, 128, 146};
    SDL_Rect inside = {frame.x + 3, frame.y + 3, frame.w - 6, frame.h - 6};
    char text[VCC_HINT_CAPACITY + 8];
    bevel(renderer, frame, 3, 0);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    (void)SDL_RenderFillRect(renderer, &inside);
    inside.x += 1; inside.y += 1; inside.w -= 2; inside.h -= 2;
    (void)snprintf(text, sizeof text, "Hint: %s", game->level->hint);
    ui_draw_hint(renderer, text, inside);
}

/* Counter flags follow 2:0CBE: time is yellow at 15 seconds or less (so an
 * untimed level shows yellow dashes), chips are yellow once none remain. */
static void draw_info(SDL_Renderer *renderer, const vcc_graphics *graphics,
    const vcc_game *game)
{
    SDL_Rect target = {INFO_X, INFO_Y, 154, 300};
    (void)SDL_RenderCopy(renderer, graphics->info, NULL, &target);
    draw_counter(renderer, graphics->digits, game->level->number, 0, 0,
        INFO_X + 47, INFO_Y + 37);
    draw_counter(renderer, graphics->digits, game->time_left,
        game->level->time_limit == 0U, game->time_left <= 15, INFO_X + 47, INFO_Y + 99);
    if (game->bottom[(size_t)game->chip_y * 32U + (size_t)game->chip_x] == VCC_HINT) {
        draw_hint(renderer, game);
        return;
    }
    draw_counter(renderer, graphics->digits, game->chips_left, 0,
        game->chips_left == 0, INFO_X + 47, INFO_Y + 189);
    draw_inventory(renderer, graphics, game);
}

/* ---- Session -------------------------------------------------------- */

#define SAVE_DIR "ux0:data/VitaChipsChallenge"
#define SAVE_PATH SAVE_DIR "/entpack.ini"
#define CAPTION "Chip's Challenge"  /* DS:0068 */
#define TICK_MS 110U                /* timer 1, 2:16FA */
#define REPEAT_DELAY_MS 250U        /* KeyboardDelay 0 (2:24D0) */
#define REPEAT_RATE_MS 33U

typedef struct app {
    SDL_Renderer *renderer;
    vcc_graphics graphics;
    vcc_audio audio;
    ui_fonts fonts;
    vcc_dat *dat;
    vcc_game game;
    vcc_progress progress;
    vcc_attempts attempts;
    uint16_t level_index;
    ui_dialog *modal;  /* dialog drawn over the board while a hook runs */
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

static void render(app *a)
{
    SDL_SetRenderDrawColor(a->renderer, 0, 128, 0, 255);
    SDL_RenderClear(a->renderer);
    draw_background(a->renderer, a->graphics.background);
    draw_board(a->renderer, &a->graphics, &a->game);
    draw_info(a->renderer, &a->graphics, &a->game);
    if (a->modal) ui_dialog_draw(a->renderer, &a->fonts, a->modal);
    SDL_RenderPresent(a->renderer);
}

/* DialogBox / MessageBox: a nested message loop that returns the chosen
 * button while the board stays as it was when the dialog opened. */
static int run_modal(app *a, ui_dialog *dialog)
{
    ui_dialog *outer = a->modal;
    int result = 0;
    a->modal = dialog;
    while (!result && !a->quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { a->quit = 1; break; }
            result = ui_dialog_event(dialog, &event);
            if (result) break;
        }
        render(a);
        SDL_Delay(8);
    }
    a->modal = outer;
    return result;
}

/* 2:0000, the message box wrapper; the death box has no beep. */
static int message_box(app *a, const char *text, int yes_no, int question)
{
    ui_dialog dialog;
    ui_message_box(&dialog, &a->fonts, CAPTION, text, yes_no, question);
    return run_modal(a, &dialog);
}

/* The parts of 4:0356 that follow its prompts. A new level is recorded as
 * visited with its password and raises "Highest Level". */
static void load_level(app *a, uint16_t index, int retry)
{
    const vcc_level *level = &a->dat->levels[index];
    vcc_level_progress *entry = &a->progress.levels[level->number];
    a->level_index = index;
    if (!retry) vcc_attempts_new_level(&a->attempts);
    (void)vcc_game_start(&a->game, level);
    if (entry->password[0] == '\0')
        memcpy(entry->password, level->password, sizeof entry->password);
    a->progress.current_level = level->number;
    if (level->number > a->progress.highest_level) a->progress.highest_level = level->number;
    progress_save(&a->progress);
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

static void hook_sound(void *context, vcc_sound_id id, int interrupt)
{
    audio_play(&((app *)context)->audio, id, interrupt);
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
    vcc_score_completion(&c, level->number, a->game.time_left,
        a->attempts.attempts, a->progress.highest_level, &entry->record,
        a->progress.current_score);
    entry->record = c.saved;
    a->progress.current_score = c.total_score;
    progress_save(&a->progress);

    ui_dialog_begin(&dialog, "Level Complete!", 136 * 6 / 4, 119 * 13 / 8);
    ui_dialog_static(&dialog, ui_dlu(9, 7, 117, 8), UI_CENTER, titles[c.title]);
    (void)snprintf(text, sizeof text, "Time Bonus:  %d", (int)c.time_bonus);
    ui_dialog_static(&dialog, ui_dlu(9, 21, 117, 8), UI_CENTER, text);
    (void)snprintf(text, sizeof text, "Level Bonus:  %ld", (long)c.level_bonus);
    ui_dialog_static(&dialog, ui_dlu(9, 35, 117, 8), UI_CENTER, text);
    (void)snprintf(text, sizeof text, "Level Score:  %ld", (long)c.level_score);
    ui_dialog_static(&dialog, ui_dlu(9, 49, 117, 8), UI_CENTER, text);
    (void)snprintf(text, sizeof text, "Total Score:  %ld", (long)c.total_score);
    ui_dialog_static(&dialog, ui_dlu(9, 63, 117, 8), UI_CENTER, text);
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
    ui_dialog_static(&dialog, ui_dlu(9, 77, 117, 19), UI_CENTER, text);
    ui_dialog_button(&dialog, ui_dlu(48, 99, 40, 14), 106, "Onward!", 1);
    (void)run_modal(a, &dialog);
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

/* 7:0CCA */
static void hook_completed(void *context, vcc_game *game)
{
    app *a = context;
    int number = game->level->number;
    show_complete(a);
    if (number == 144 || number == 149) {
        /* The exit animation and ending (7:0A74) are not reconstructed yet;
         * show its messages in order. */
        char text[512];
        int completed = 0;
        int level;
        (void)message_box(a, "Great Job, Chip!\nYou did it!  You finished the challenge!", 0, 0);
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
        load_level(a, a->level_index, 1);
        return;
    }
    if (number % 10 == 0 && number / 10 >= 5 && number / 10 <= 14)
        (void)message_box(a, interludes[number / 10 - 5], 0, 0);
    next_level(a);
}

static uint16_t level_index_for(const vcc_dat *dat, uint16_t number)
{
    uint16_t index;
    for (index = 0U; index < dat->level_count; ++index)
        if (dat->levels[index].number == number) return index;
    return 0U;
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
    a->renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : NULL;
    if (!a->renderer || SDL_RenderSetScale(a->renderer, 1.5f, 1.5f) != 0) goto cleanup;
    a->graphics.tiles = load_bmp(a->renderer, "app0:/data/OBJ32_4_RGB.bmp");
    a->graphics.actors = load_masked_tiles(a->renderer);
    a->graphics.background = load_bmp(a->renderer, "app0:/data/BACKGROUND_RGB.bmp");
    a->graphics.info = load_bmp(a->renderer, "app0:/data/INFOWND_RGB.bmp");
    a->graphics.digits = load_bmp(a->renderer, "app0:/data/200_RGB.bmp");
    if (!a->graphics.tiles || !a->graphics.actors || !a->graphics.background
        || !a->graphics.info || !a->graphics.digits) goto cleanup;
    if (!ui_fonts_open(&a->fonts, "app0:/data/fonts/LiberationSans-Regular.ttf",
            "app0:/data/fonts/LiberationSans-Bold.ttf")) goto cleanup;
    if (!audio_init(&a->audio)) goto cleanup;
    if (SDL_NumJoysticks() > 0 && SDL_IsGameController(0)) controller = SDL_GameControllerOpen(0);

    a->game.hooks.context = a;
    a->game.hooks.sound = hook_sound;
    a->game.hooks.died = hook_died;
    a->game.hooks.completed = hook_completed;
    vcc_game_seed(&a->game, SDL_GetTicks());  /* srand(GetCurrentTime()), 2:092E */
    /* Startup resumes at "Current Level" with "Current Score" (2:0B24). */
    progress_load(&a->progress);
    load_level(a, level_index_for(a->dat, a->progress.current_level), 0);

#ifdef VCC_PREVIEW
    if (VCC_PREVIEW == 1) { a->game.death = VCC_DEATH_FIRE; hook_died(a, &a->game); }
    if (VCC_PREVIEW == 3) { a->attempts.attempts = 2; show_complete(a); }
#endif
    next_tick = SDL_GetTicks() + TICK_MS;
    while (!a->quit) {
        SDL_Event event;
        uint32_t now;
        while (SDL_PollEvent(&event)) {
            vcc_direction direction = VCC_DIR_NONE;
            if (event.type == SDL_QUIT) a->quit = 1;
            if (event.type == SDL_KEYDOWN) {
                /* Windows key repeat drives held keys (2:2540). */
                direction = key_direction(event.key.keysym.sym);
                if (event.key.keysym.sym == SDLK_r && (SDL_GetModState() & KMOD_CTRL))
                    retry_level(a);
                else vcc_game_key(&a->game, direction);
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                direction = button_direction(event.cbutton.button);
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
                    retry_level(a);  /* Level > Restart, Ctrl+R */
                } else {
                    vcc_game_key(&a->game, direction);
                    if (direction != VCC_DIR_NONE) {
                        held = direction;
                        next_repeat = SDL_GetTicks() + REPEAT_DELAY_MS;
                    }
                }
            }
            if (event.type == SDL_CONTROLLERBUTTONUP
                && button_direction(event.cbutton.button) == held)
                held = VCC_DIR_NONE;
        }
        now = SDL_GetTicks();
        if (held != VCC_DIR_NONE && (int32_t)(now - next_repeat) >= 0) {
            vcc_game_key(&a->game, held);
            next_repeat = now + REPEAT_RATE_MS;
        }
        if (a->game.waiting) {
            next_tick = now + TICK_MS;
        } else if ((int32_t)(now - next_tick) >= 0) {
            next_tick += TICK_MS;
            if ((int32_t)(now - next_tick) > (int32_t)(4U * TICK_MS)) next_tick = now + TICK_MS;
            vcc_game_tick(&a->game);
            now = SDL_GetTicks();
            if ((int32_t)(now - next_tick) > 0) next_tick = now + TICK_MS;
        }
        render(a);
        SDL_Delay(4);
    }
cleanup:
    audio_quit(&a->audio);
    ui_fonts_close(&a->fonts);
    if (controller) SDL_GameControllerClose(controller);
    if (a->graphics.digits) SDL_DestroyTexture(a->graphics.digits);
    if (a->graphics.info) SDL_DestroyTexture(a->graphics.info);
    if (a->graphics.background) SDL_DestroyTexture(a->graphics.background);
    if (a->graphics.actors) SDL_DestroyTexture(a->graphics.actors);
    if (a->graphics.tiles) SDL_DestroyTexture(a->graphics.tiles);
    if (a->renderer) SDL_DestroyRenderer(a->renderer);
    if (window) SDL_DestroyWindow(window);
    if (TTF_WasInit()) TTF_Quit();
    SDL_Quit();
    free(bytes);
    free(a->dat);
    return 0;
}
