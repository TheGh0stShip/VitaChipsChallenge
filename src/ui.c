/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#define FRAME 4         /* modal frame: black, active border, black */
#define CAPTION_H 18
#define BUTTON_W 60     /* 40 DLU */
#define BUTTON_H 23     /* 14 DLU */

static const SDL_Color black = {0, 0, 0, 255};
static const SDL_Color white = {255, 255, 255, 255};
static const SDL_Color gray = {128, 128, 128, 255};
static const SDL_Color light = {192, 192, 192, 255};
static const SDL_Color navy = {0, 0, 128, 255};

/* ---- Fonts ---------------------------------------------------------- */

/* The 640x363 logical screen is shown 1.5x on the Vita's 960x544 panel.
 * Text is rasterized at panel resolution and placed on whole panel pixels
 * so glyph strokes stay crisp instead of being resampled. */
#define TO_LOGICAL(v) ((v) * 2 / 3)

typedef struct font_slot {
    ui_face face;
    int points;
    int italic;
    TTF_Font *font;
} font_slot;

static char regular_font_path[256];
static char bold_font_path[256];
static font_slot font_slots[32];
static int font_slot_count;

static void text_cache_flush(void);

int ui_init(const char *regular_path, const char *bold_path)
{
    (void)snprintf(regular_font_path, sizeof regular_font_path, "%s", regular_path);
    (void)snprintf(bold_font_path, sizeof bold_font_path, "%s", bold_path);
    return ui_font(UI_FACE_DIALOG, 8, 0) && ui_font(UI_FACE_SYSTEM, 10, 0);
}

void ui_quit(void)
{
    int index;
    text_cache_flush();
    for (index = 0; index < font_slot_count; ++index)
        if (font_slots[index].font) TTF_CloseFont(font_slots[index].font);
    font_slot_count = 0;
}

TTF_Font *ui_font(ui_face face, int points, int italic)
{
    int index;
    int pixels;
    const char *path;
    TTF_Font *font;
    for (index = 0; index < font_slot_count; ++index) {
        font_slot *slot = &font_slots[index];
        if (slot->face == face && slot->points == points && slot->italic == italic)
            return slot->font;
    }
    if (font_slot_count >= (int)(sizeof font_slots / sizeof font_slots[0])) return NULL;
    /* Points at 96 dpi are 4/3 pixels, rasterized at the 1.5x panel scale. */
    pixels = points * 2;
    path = (face == UI_FACE_DIALOG || face == UI_FACE_ARIAL) ? regular_font_path : bold_font_path;
    if (face == UI_FACE_DIALOG) pixels = 16;  /* MS Sans Serif 8, 11 px cell */
    if (face == UI_FACE_SYSTEM) pixels = 17;  /* System, 16 px cell */
    font = TTF_OpenFont(path, pixels);
    if (font) {
        TTF_SetFontHinting(font, TTF_HINTING_MONO);
        if (italic) TTF_SetFontStyle(font, TTF_STYLE_ITALIC);
    }
    font_slots[font_slot_count].face = face;
    font_slots[font_slot_count].points = points;
    font_slots[font_slot_count].italic = italic;
    font_slots[font_slot_count].font = font;
    ++font_slot_count;
    return font;
}

int ui_line_height(TTF_Font *font)
{
    return TO_LOGICAL(TTF_FontLineSkip(font));
}

int ui_text_width(TTF_Font *font, const char *text)
{
    int w = 0;
    int h = 0;
    if (!font || !text[0]) return 0;
    (void)TTF_SizeText(font, text, &w, &h);
    return TO_LOGICAL(w);
}

/* Rasterized strings are cached; the screen redraws every frame. */
#define TEXT_CACHE 96

typedef struct text_entry {
    TTF_Font *font;
    Uint32 color;
    char text[UI_TEXT_CAPACITY];
    SDL_Texture *texture;
    int w;
    int h;
    Uint32 used;
} text_entry;

static text_entry text_cache[TEXT_CACHE];
static Uint32 text_clock;

static void text_cache_flush(void)
{
    int index;
    for (index = 0; index < TEXT_CACHE; ++index) {
        if (text_cache[index].texture) SDL_DestroyTexture(text_cache[index].texture);
        memset(&text_cache[index], 0, sizeof text_cache[index]);
    }
}

void ui_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y,
    SDL_Color color)
{
    Uint32 key = ((Uint32)color.r << 16) | ((Uint32)color.g << 8) | color.b;
    text_entry *slot = &text_cache[0];
    SDL_FRect target;
    int index;
    if (!font || !text[0]) return;
    for (index = 0; index < TEXT_CACHE; ++index) {
        text_entry *entry = &text_cache[index];
        if (entry->texture && entry->font == font && entry->color == key
            && strcmp(entry->text, text) == 0) {
            slot = entry;
            goto draw;
        }
        if (entry->used < slot->used) slot = entry;
    }
    {
        SDL_Surface *surface = TTF_RenderText_Solid(font, text, color);
        if (!surface) return;
        if (slot->texture) SDL_DestroyTexture(slot->texture);
        slot->texture = SDL_CreateTextureFromSurface(renderer, surface);
        slot->font = font;
        slot->color = key;
        (void)snprintf(slot->text, sizeof slot->text, "%s", text);
        slot->w = surface->w;
        slot->h = surface->h;
        SDL_FreeSurface(surface);
        if (!slot->texture) return;
    }
draw:
    slot->used = ++text_clock;
    /* Snap to the panel grid: logical x maps to panel pixel floor(1.5x). */
    target.x = (float)(x * 3 / 2) / 1.5f;
    target.y = (float)(y * 3 / 2) / 1.5f;
    target.w = (float)slot->w / 1.5f;
    target.h = (float)slot->h / 1.5f;
    (void)SDL_RenderCopyF(renderer, slot->texture, NULL, &target);
}

/* Splits text into lines no wider than `width`, like DT_WORDBREAK. */
static int wrap_text(TTF_Font *font, const char *text, int width,
    char lines[][UI_TEXT_CAPACITY], int max_lines)
{
    int count = 0;
    const char *cursor = text;
    while (*cursor && count < max_lines) {
        char line[UI_TEXT_CAPACITY] = "";
        size_t used = 0U;
        const char *word = cursor;
        if (*cursor == '\r') { ++cursor; continue; }
        while (*word && *word != '\n' && *word != '\r') {
            const char *end = word;
            char candidate[UI_TEXT_CAPACITY];
            size_t length;
            while (*end && *end != ' ' && *end != '\n' && *end != '\r') ++end;
            while (*end == ' ') ++end;
            if (used + (size_t)(end - word) >= sizeof candidate) break;
            memcpy(candidate, line, used);
            memcpy(candidate + used, word, (size_t)(end - word));
            candidate[used + (size_t)(end - word)] = '\0';
            length = strlen(candidate);
            {
                char measured[UI_TEXT_CAPACITY];
                memcpy(measured, candidate, length + 1U);
                while (length > 0U && measured[length - 1U] == ' ') measured[--length] = '\0';
                if (used > 0U && ui_text_width(font, measured) > width) break;
            }
            memcpy(line, candidate, strlen(candidate) + 1U);
            used = strlen(line);
            word = end;
        }
        while (used > 0U && line[used - 1U] == ' ') line[--used] = '\0';
        memcpy(lines[count++], line, used + 1U);
        cursor = word;
        if (*cursor == '\r') ++cursor;
        if (*cursor == '\n') ++cursor;
    }
    return count;
}

int ui_measure_text(TTF_Font *font, const char *text, int width, int *out_width)
{
    char lines[24][UI_TEXT_CAPACITY];
    int count = wrap_text(font, text, width, lines, 24);
    int index;
    int widest = 0;
    for (index = 0; index < count; ++index) {
        int w = ui_text_width(font, lines[index]);
        if (w > widest) widest = w;
    }
    if (out_width) *out_width = widest;
    return count * ui_line_height(font);
}

int ui_draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text,
    SDL_Rect rect, ui_align align, SDL_Color color)
{
    char lines[24][UI_TEXT_CAPACITY];
    int count;
    int line_h;
    int index;
    if (!font) return 0;
    count = wrap_text(font, text, rect.w, lines, 24);
    line_h = ui_line_height(font);
    for (index = 0; index < count; ++index) {
        int w = ui_text_width(font, lines[index]);
        int x = rect.x;
        if (align == UI_CENTER) x = rect.x + (rect.w - w) / 2;
        else if (align == UI_RIGHT) x = rect.x + rect.w - w;
        ui_text(renderer, font, lines[index], x, rect.y + index * line_h, color);
    }
    return count * line_h;
}

void ui_draw_fitted(SDL_Renderer *renderer, ui_face face, int italic, int points,
    const char *text, SDL_Rect rect, SDL_Color color)
{
    for (; points >= 6; --points) {
        TTF_Font *font = ui_font(face, points, italic);
        if (!font) continue;
        if (ui_measure_text(font, text, rect.w, NULL) <= rect.h || points == 6) {
            (void)ui_draw_text(renderer, font, text, rect, UI_CENTER, color);
            return;
        }
    }
}

/* ---- Drawing primitives -------------------------------------------- */

static void color(SDL_Renderer *renderer, SDL_Color c)
{
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
}

static void fill(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color c)
{
    SDL_Rect rect = {x, y, w, h};
    color(renderer, c);
    (void)SDL_RenderFillRect(renderer, &rect);
}

void ui_fill(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color c)
{
    fill(renderer, rect.x, rect.y, rect.w, rect.h, c);
}

void ui_bevel(SDL_Renderer *renderer, SDL_Rect r, int depth, int raised)
{
    int i;
    for (i = 1; i <= depth; ++i) {
        SDL_Rect o = {r.x - i, r.y - i, r.w + 2 * i, r.h + 2 * i};
        SDL_Color tl = raised ? white : gray;
        SDL_Color br = raised ? gray : white;
        fill(renderer, o.x, o.y, o.w - 1, 1, tl);
        fill(renderer, o.x, o.y, 1, o.h - 1, tl);
        fill(renderer, o.x + 1, o.y + o.h - 1, o.w - 1, 1, br);
        fill(renderer, o.x + o.w - 1, o.y + 1, 1, o.h - 1, br);
    }
}

static void frame_rect(SDL_Renderer *renderer, SDL_Rect r, SDL_Color c)
{
    fill(renderer, r.x, r.y, r.w, 1, c);
    fill(renderer, r.x, r.y + r.h - 1, r.w, 1, c);
    fill(renderer, r.x, r.y, 1, r.h, c);
    fill(renderer, r.x + r.w - 1, r.y, 1, r.h, c);
}

static void focus_rect(SDL_Renderer *renderer, SDL_Rect r)
{
    int i;
    color(renderer, black);
    for (i = 0; i < r.w; i += 2) {
        (void)SDL_RenderDrawPoint(renderer, r.x + i, r.y);
        (void)SDL_RenderDrawPoint(renderer, r.x + i, r.y + r.h - 1);
    }
    for (i = 0; i < r.h; i += 2) {
        (void)SDL_RenderDrawPoint(renderer, r.x, r.y + i);
        (void)SDL_RenderDrawPoint(renderer, r.x + r.w - 1, r.y + i);
    }
}

/* Draws "&Label" with the mnemonic underlined; returns the text width. */
static int mnemonic_text(SDL_Renderer *renderer, TTF_Font *font, const char *label,
    int x, int y, SDL_Color c, int measure_only)
{
    char text[64];
    int underline = -1;
    size_t in = 0U;
    size_t out = 0U;
    while (label[in] && label[in] != '\t' && out + 1U < sizeof text) {
        if (label[in] == '&' && label[in + 1]) {
            underline = (int)out;
            ++in;
        }
        text[out++] = label[in++];
    }
    text[out] = '\0';
    if (!measure_only) {
        ui_text(renderer, font, text, x, y, c);
        if (underline >= 0) {
            char prefix[64];
            char one[2] = {text[underline], '\0'};
            memcpy(prefix, text, (size_t)underline);
            prefix[underline] = '\0';
            fill(renderer, x + ui_text_width(font, prefix),
                y + TO_LOGICAL(TTF_FontAscent(font)) + 1, ui_text_width(font, one), 1, c);
        }
    }
    return ui_text_width(font, text);
}

static char mnemonic_of(const char *label)
{
    const char *amp = strchr(label, '&');
    char c;
    if (!amp || !amp[1]) return 0;
    c = amp[1];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return c;
}

/* ---- Dialogs -------------------------------------------------------- */

SDL_Rect ui_dlu(int x, int y, int cx, int cy)
{
    SDL_Rect rect = {x * 6 / 4, y * 13 / 8, cx * 6 / 4, cy * 13 / 8};
    return rect;
}

SDL_Rect ui_dlu_system(int x, int y, int cx, int cy)
{
    SDL_Rect rect = {x * 8 / 4, y * 16 / 8, cx * 8 / 4, cy * 16 / 8};
    return rect;
}

void ui_dialog_begin(ui_dialog *dialog, const char *caption, int client_w, int client_h)
{
    int outer_w = client_w + 2 * FRAME;
    int outer_h = client_h + 2 * FRAME + CAPTION_H + 1;
    memset(dialog, 0, sizeof *dialog);
    dialog->open = 1;
    dialog->face = UI_FACE_DIALOG;
    (void)snprintf(dialog->caption, sizeof dialog->caption, "%s", caption);
    dialog->client.x = (UI_SCREEN_W - outer_w) / 2 + FRAME;
    dialog->client.y = (UI_SCREEN_H - outer_h) / 2 + FRAME + CAPTION_H + 1;
    dialog->client.w = client_w;
    dialog->client.h = client_h;
    dialog->focus = -1;
}

static int focusable(const ui_control *control)
{
    return !control->disabled
        && (control->kind == UI_BUTTON || control->kind == UI_EDIT || control->kind == UI_LIST);
}

ui_control *ui_add(ui_dialog *dialog, ui_kind kind, int id, SDL_Rect rect, const char *text)
{
    ui_control *control;
    if (dialog->count >= UI_MAX_CONTROLS) return NULL;
    control = &dialog->controls[dialog->count];
    memset(control, 0, sizeof *control);
    control->kind = kind;
    control->id = id;
    control->rect = rect;
    control->max_length = 10;
    (void)snprintf(control->text, sizeof control->text, "%s", text ? text : "");
    if (dialog->focus < 0 && focusable(control)) dialog->focus = dialog->count;
    ++dialog->count;
    return control;
}

ui_control *ui_find(ui_dialog *dialog, int id)
{
    int index;
    for (index = 0; index < dialog->count; ++index)
        if (dialog->controls[index].id == id) return &dialog->controls[index];
    return NULL;
}

void ui_set_text(ui_dialog *dialog, int id, const char *text)
{
    ui_control *control = ui_find(dialog, id);
    if (control) (void)snprintf(control->text, sizeof control->text, "%s", text);
}

void ui_focus(ui_dialog *dialog, int id)
{
    int index;
    for (index = 0; index < dialog->count; ++index) {
        if (dialog->controls[index].id == id) {
            dialog->focus = index;
            if (dialog->controls[index].kind == UI_EDIT) dialog->controls[index].selected_all = 1;
        }
    }
}

void ui_message_box(ui_dialog *dialog, const char *caption, const char *text,
    int yes_no, int icon)
{
    TTF_Font *font = ui_font(UI_FACE_SYSTEM, 10, 0);
    const int margin = 12;
    const int icon_space = icon ? 32 + 16 : 0;
    int text_w = 0;
    int text_h = ui_measure_text(font, text, 330, &text_w);
    int buttons_w = yes_no ? 2 * BUTTON_W + 12 : BUTTON_W;
    int content_w = icon_space + text_w;
    int client_w = (content_w > buttons_w ? content_w : buttons_w) + 2 * 16;
    int body_h = icon && text_h < 32 ? 32 : text_h;
    int button_y = margin + body_h + margin;
    int x;
    SDL_Rect rect;
    ui_control *label;
    ui_dialog_begin(dialog, caption, client_w, button_y + BUTTON_H + 10);
    dialog->face = UI_FACE_SYSTEM;
    dialog->icon = icon;
    rect.x = 16 + icon_space;
    rect.y = margin + (icon && text_h < 32 ? (32 - text_h) / 2 : 0);
    rect.w = text_w + 1;
    rect.h = text_h;
    label = ui_add(dialog, UI_STATIC, -1, rect, text);
    if (label) label->align = UI_LEFT;
    x = (client_w - buttons_w) / 2;
    if (yes_no) {
        SDL_Rect yes = {x, button_y, BUTTON_W, BUTTON_H};
        SDL_Rect no = {x + BUTTON_W + 12, button_y, BUTTON_W, BUTTON_H};
        ui_control *button = ui_add(dialog, UI_BUTTON, 6, yes, "&Yes");
        if (button) button->is_default = 1;
        (void)ui_add(dialog, UI_BUTTON, 7, no, "&No");
        dialog->cancel_id = 0;
    } else {
        SDL_Rect ok = {x, button_y, BUTTON_W, BUTTON_H};
        ui_control *button = ui_add(dialog, UI_BUTTON, 1, ok, "OK");
        if (button) button->is_default = 1;
        dialog->cancel_id = 1;
    }
}

static int next_focus(const ui_dialog *dialog, int from, int step)
{
    int index = from;
    int tries;
    for (tries = 0; tries < dialog->count; ++tries) {
        index = (index + step + dialog->count) % dialog->count;
        if (focusable(&dialog->controls[index])) return index;
    }
    return from;
}

static int default_id(const ui_dialog *dialog)
{
    int index;
    if (dialog->focus >= 0 && dialog->controls[dialog->focus].kind == UI_BUTTON)
        return dialog->controls[dialog->focus].id;
    for (index = 0; index < dialog->count; ++index)
        if (dialog->controls[index].kind == UI_BUTTON && dialog->controls[index].is_default)
            return dialog->controls[index].id;
    return 0;
}

static const int list_item_h = 13;

static int list_rows(const ui_control *list)
{
    int rows = (list->rect.h - 2) / list_item_h;
    return rows < 1 ? 1 : rows;
}

static void list_select(ui_control *list, int selection)
{
    int rows = list_rows(list);
    if (list->item_count == 0) return;
    if (selection < 0) selection = 0;
    if (selection >= list->item_count) selection = list->item_count - 1;
    list->selection = selection;
    if (selection < list->top) list->top = selection;
    if (selection >= list->top + rows) list->top = selection - rows + 1;
}

static int point_in(SDL_Rect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

int ui_dialog_event(ui_dialog *dialog, const SDL_Event *event, ui_notify *notify)
{
    ui_control *focused = dialog->focus >= 0 ? &dialog->controls[dialog->focus] : NULL;
    notify->id = 0;
    notify->code = 0;
    if (!dialog->open) return 0;
    if (event->type == SDL_TEXTINPUT && focused && focused->kind == UI_EDIT) {
        size_t length;
        if (focused->selected_all) { focused->text[0] = '\0'; focused->selected_all = 0; }
        length = strlen(focused->text);
        if ((int)(length + strlen(event->text.text)) <= focused->max_length)
            (void)snprintf(focused->text + length, sizeof focused->text - length, "%s",
                event->text.text);
        return 0;
    }
    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        int index;
        if (focused && focused->kind == UI_EDIT && key == SDLK_BACKSPACE) {
            size_t length = strlen(focused->text);
            if (focused->selected_all) focused->text[0] = '\0';
            else if (length > 0U) focused->text[length - 1U] = '\0';
            focused->selected_all = 0;
            return 0;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            notify->id = default_id(dialog);
            return notify->id != 0;
        }
        if (key == SDLK_SPACE && focused && focused->kind == UI_BUTTON) {
            notify->id = focused->id;
            return 1;
        }
        if (key == SDLK_ESCAPE) {
            notify->id = dialog->cancel_id;
            return notify->id != 0;
        }
        if (key == SDLK_TAB) {
            dialog->focus = next_focus(dialog, dialog->focus,
                (SDL_GetModState() & KMOD_SHIFT) ? -1 : 1);
            return 0;
        }
        if (focused && focused->kind == UI_LIST && (key == SDLK_UP || key == SDLK_DOWN)) {
            list_select(focused, focused->selection + (key == SDLK_UP ? -1 : 1));
            notify->id = focused->id;
            notify->code = 1;
            return 1;
        }
        if (!focused || focused->kind != UI_EDIT) {
            if (key == SDLK_RIGHT || key == SDLK_DOWN) {
                dialog->focus = next_focus(dialog, dialog->focus, 1);
                return 0;
            }
            if (key == SDLK_LEFT || key == SDLK_UP) {
                dialog->focus = next_focus(dialog, dialog->focus, -1);
                return 0;
            }
            for (index = 0; index < dialog->count; ++index) {
                ui_control *c = &dialog->controls[index];
                if (c->kind == UI_BUTTON && !c->disabled && mnemonic_of(c->text) == (char)key) {
                    notify->id = c->id;
                    return 1;
                }
            }
        }
    }
    if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 button = event->cbutton.button;
        if (button == SDL_CONTROLLER_BUTTON_A) {
            if (focused && focused->kind == UI_EDIT) {
                /* The Vita IME dialog replaces the field's contents. */
                focused->selected_all = 1;
                SDL_StartTextInput();
                return 0;
            }
            if (focused && focused->kind == UI_LIST) {
                notify->id = focused->id;
                notify->code = 2;
                return 1;
            }
            notify->id = default_id(dialog);
            return notify->id != 0;
        }
        if (button == SDL_CONTROLLER_BUTTON_B) {
            notify->id = dialog->cancel_id;
            return notify->id != 0;
        }
        if (focused && focused->kind == UI_LIST
            && (button == SDL_CONTROLLER_BUTTON_DPAD_UP
                || button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
            list_select(focused, focused->selection
                + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1));
            notify->id = focused->id;
            notify->code = 1;
            return 1;
        }
        if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
            dialog->focus = next_focus(dialog, dialog->focus, 1);
            return 0;
        }
        if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT || button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
            dialog->focus = next_focus(dialog, dialog->focus, -1);
            return 0;
        }
    }
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int x = event->button.x - dialog->client.x;
        int y = event->button.y - dialog->client.y;
        int index;
        for (index = 0; index < dialog->count; ++index) {
            ui_control *c = &dialog->controls[index];
            if (!focusable(c) || !point_in(c->rect, x, y)) continue;
            dialog->focus = index;
            if (c->kind == UI_BUTTON) {
                notify->id = c->id;
                return 1;
            }
            if (c->kind == UI_EDIT) {
                c->selected_all = 1;
                SDL_StartTextInput();
                return 0;
            }
            if (c->kind == UI_LIST) {
                int row = c->top + (y - c->rect.y - 1) / list_item_h;
                if (x >= c->rect.x + c->rect.w - 17) {
                    list_select(c, c->selection + (y < c->rect.y + c->rect.h / 2 ? -1 : 1));
                    notify->id = c->id;
                    notify->code = 1;
                    return 1;
                }
                if (row >= 0 && row < c->item_count) {
                    int again = row == c->selection && event->button.clicks >= 2;
                    list_select(c, row);
                    notify->id = c->id;
                    notify->code = again ? 2 : 1;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Windows 3.1 push button: rounded black outline (thicker when default),
 * 2px white highlight and 2px gray shadow around a gray face. */
static void draw_button(SDL_Renderer *renderer, TTF_Font *font, const ui_control *c,
    SDL_Rect r, int is_default, int focused)
{
    int border = is_default ? 2 : 1;
    int w = mnemonic_text(renderer, font, c->text, 0, 0, black, 1);
    int line_h = TO_LOGICAL(TTF_FontHeight(font));
    int tx = r.x + (r.w - w) / 2;
    int ty = r.y + (r.h - line_h) / 2;
    fill(renderer, r.x + 1, r.y, r.w - 2, r.h, black);
    fill(renderer, r.x, r.y + 1, r.w, r.h - 2, black);
    fill(renderer, r.x + border, r.y + border, r.w - 2 * border, r.h - 2 * border, light);
    fill(renderer, r.x + border, r.y + border, r.w - 2 * border - 1, 2, white);
    fill(renderer, r.x + border, r.y + border, 2, r.h - 2 * border - 1, white);
    fill(renderer, r.x + border, r.y + r.h - border - 2, r.w - 2 * border, 2, gray);
    fill(renderer, r.x + r.w - border - 2, r.y + border, 2, r.h - 2 * border, gray);
    (void)mnemonic_text(renderer, font, c->text, tx, ty, c->disabled ? gray : black, 0);
    if (focused) {
        SDL_Rect f = {tx - 2, ty - 1, w + 4, line_h + 2};
        focus_rect(renderer, f);
    }
}

static void draw_icon(SDL_Renderer *renderer, int kind, int x, int y)
{
    /* Approximations of the Windows 3.1 message box icons; the system icons
     * are not part of the reference archive. */
    TTF_Font *font = ui_font(UI_FACE_ARIAL_BOLD, 16, 0);
    const char *glyph = kind == 1 ? "?" : (kind == 2 ? "STOP" : "!");
    SDL_Color face = kind == 2 ? (SDL_Color){255, 0, 0, 255}
        : (kind == 3 ? (SDL_Color){255, 255, 0, 255} : white);
    SDL_Color ink = kind == 1 ? navy : (kind == 2 ? white : black);
    int dy;
    for (dy = -14; dy <= 14; ++dy) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= 14 * 14) ++dx;
        fill(renderer, x + 16 - dx - 1, y + 16 + dy, 2 * dx + 2, 1, black);
        if (dx > 0) fill(renderer, x + 16 - dx, y + 16 + dy, 2 * dx, 1, face);
    }
    if (kind == 2) font = ui_font(UI_FACE_ARIAL_BOLD, 7, 0);
    ui_text(renderer, font, glyph, x + 16 - ui_text_width(font, glyph) / 2,
        y + 16 - TO_LOGICAL(TTF_FontHeight(font)) / 2, ink);
}

static void draw_scrollbar(SDL_Renderer *renderer, SDL_Rect r, int top, int rows, int count)
{
    SDL_Rect up = {r.x, r.y, r.w, r.w};
    SDL_Rect down = {r.x, r.y + r.h - r.w, r.w, r.w};
    fill(renderer, r.x, r.y, r.w, r.h, light);
    frame_rect(renderer, r, black);
    fill(renderer, up.x + 1, up.y + 1, up.w - 2, up.h - 2, light);
    frame_rect(renderer, up, black);
    frame_rect(renderer, down, black);
    {
        /* Arrows drawn as filled triangles. */
        int i;
        for (i = 0; i < 4; ++i) {
            fill(renderer, up.x + up.w / 2 - i, up.y + 5 + i, 2 * i + 1, 1, black);
            fill(renderer, down.x + down.w / 2 - i, down.y + down.h - 6 - i, 2 * i + 1, 1, black);
        }
    }
    if (count > rows) {
        int track = r.h - 2 * r.w;
        int position = r.y + r.w + (track - r.w) * top / (count - rows);
        SDL_Rect thumb = {r.x, position, r.w, r.w};
        fill(renderer, thumb.x, thumb.y, thumb.w, thumb.h, white);
        frame_rect(renderer, thumb, black);
    }
}

void ui_dialog_draw(SDL_Renderer *renderer, const ui_dialog *dialog)
{
    TTF_Font *font = ui_font(dialog->face, dialog->face == UI_FACE_DIALOG ? 8 : 10, 0);
    TTF_Font *caption = ui_font(UI_FACE_SYSTEM, 10, 0);
    SDL_Rect c = dialog->client;
    int outer_x = c.x - FRAME;
    int outer_y = c.y - FRAME - CAPTION_H - 1;
    int outer_w = c.w + 2 * FRAME;
    int outer_h = c.h + 2 * FRAME + CAPTION_H + 1;
    int index;
    if (!dialog->open) return;
    /* Modal frame. */
    fill(renderer, outer_x, outer_y, outer_w, outer_h, black);
    fill(renderer, outer_x + 1, outer_y + 1, outer_w - 2, outer_h - 2, navy);
    fill(renderer, outer_x + FRAME - 1, outer_y + FRAME - 1,
        outer_w - 2 * FRAME + 2, outer_h - 2 * FRAME + 2, black);
    /* Caption bar with system menu box. */
    if (dialog->caption[0] || 1) {
        SDL_Rect title = {c.x + CAPTION_H + 1, outer_y + FRAME
            + (CAPTION_H - TO_LOGICAL(TTF_FontHeight(caption))) / 2,
            c.w - CAPTION_H - 1, CAPTION_H};
        fill(renderer, c.x, outer_y + FRAME, c.w, CAPTION_H, navy);
        fill(renderer, c.x, outer_y + FRAME, CAPTION_H, CAPTION_H, light);
        fill(renderer, c.x + CAPTION_H, outer_y + FRAME, 1, CAPTION_H, black);
        fill(renderer, c.x + 4, outer_y + FRAME + 8, 11, 3, gray);
        fill(renderer, c.x + 3, outer_y + FRAME + 7, 11, 3, white);
        (void)ui_draw_text(renderer, caption, dialog->caption, title, UI_CENTER, white);
    }
    fill(renderer, c.x, c.y - 1, c.w, 1, black);
    /* Client: GRAYDLGPROC paints light gray. */
    fill(renderer, c.x, c.y, c.w, c.h, light);
    if (dialog->icon) draw_icon(renderer, dialog->icon, c.x + 16, c.y + 10);
    for (index = 0; index < dialog->count; ++index) {
        const ui_control *control = &dialog->controls[index];
        SDL_Rect r = {c.x + control->rect.x, c.y + control->rect.y,
            control->rect.w, control->rect.h};
        int focused = index == dialog->focus;
        switch (control->kind) {
        case UI_STATIC:
            (void)ui_draw_text(renderer, font, control->text, r, control->align, black);
            break;
        case UI_IMAGE:
            if (control->image) (void)SDL_RenderCopy(renderer, control->image,
                &control->image_source, &r);
            break;
        case UI_BUTTON: {
            int is_default = focused || (control->is_default
                && !(dialog->focus >= 0 && dialog->controls[dialog->focus].kind == UI_BUTTON));
            draw_button(renderer, font, control, r, is_default, focused);
            break;
        }
        case UI_EDIT: {
            int ty = r.y + (r.h - TO_LOGICAL(TTF_FontHeight(font))) / 2;
            int w = ui_text_width(font, control->text);
            fill(renderer, r.x, r.y, r.w, r.h, white);
            frame_rect(renderer, r, black);
            if (focused && control->selected_all && control->text[0]) {
                fill(renderer, r.x + 2, ty, w + 1, TO_LOGICAL(TTF_FontHeight(font)), navy);
                ui_text(renderer, font, control->text, r.x + 2, ty, white);
            } else {
                ui_text(renderer, font, control->text, r.x + 2, ty, black);
                if (focused && (SDL_GetTicks() / 500U) % 2U == 0U)
                    fill(renderer, r.x + 2 + w, ty, 1, TO_LOGICAL(TTF_FontHeight(font)), black);
            }
            break;
        }
        case UI_LIST: {
            int rows = list_rows(control);
            int row;
            SDL_Rect bar = {r.x + r.w - 17, r.y, 17, r.h};
            fill(renderer, r.x, r.y, r.w, r.h, white);
            frame_rect(renderer, r, black);
            for (row = 0; row < rows && control->top + row < control->item_count; ++row) {
                int item = control->top + row;
                SDL_Rect line = {r.x + 1, r.y + 1 + row * list_item_h, r.w - 19, list_item_h};
                SDL_Color ink = black;
                if (item == control->selection) {
                    fill(renderer, line.x, line.y, line.w, line.h, navy);
                    ink = white;
                    if (focused) focus_rect(renderer, line);
                }
                ui_text(renderer, font, control->items[item], line.x + 2, line.y, ink);
            }
            draw_scrollbar(renderer, bar, control->top, rows, control->item_count);
            break;
        }
        }
    }
}

/* ---- Menus ---------------------------------------------------------- */

static int menu_title_x(const ui_menu_bar *bar, int index, TTF_Font *font)
{
    int x = 0;
    int i;
    for (i = 0; i < index; ++i)
        x += mnemonic_text(NULL, font, bar->menus[i].label, 0, 0, black, 1) + 16;
    return x;
}

static SDL_Rect popup_rect(const ui_menu_bar *bar, TTF_Font *font)
{
    const ui_menu *menu = &bar->menus[bar->active];
    int width = 0;
    int height = 2;
    int i;
    SDL_Rect r;
    for (i = 0; i < menu->count; ++i) {
        const ui_menu_item *item = &menu->items[i];
        if (!item->label) { height += 8; continue; }
        {
            const char *tab = strchr(item->label, '\t');
            int w = mnemonic_text(NULL, font, item->label, 0, 0, black, 1)
                + (tab ? ui_text_width(font, tab + 1) + 24 : 0);
            if (w > width) width = w;
        }
        height += 16;
    }
    r.x = menu_title_x(bar, bar->active, font);
    r.y = UI_MENU_BAR_H - 1;
    r.w = width + 36;
    r.h = height;
    return r;
}

void ui_menu_draw(SDL_Renderer *renderer, const ui_menu_bar *bar)
{
    TTF_Font *font = ui_font(UI_FACE_SYSTEM, 10, 0);
    int i;
    int x = 0;
    fill(renderer, 0, 0, UI_SCREEN_W, UI_MENU_BAR_H - 1, white);
    fill(renderer, 0, UI_MENU_BAR_H - 1, UI_SCREEN_W, 1, black);
    for (i = 0; i < bar->count; ++i) {
        int w = mnemonic_text(renderer, font, bar->menus[i].label, 0, 0, black, 1);
        SDL_Color ink = black;
        if (i == bar->active) {
            fill(renderer, x, 0, w + 16, UI_MENU_BAR_H - 1, navy);
            ink = white;
        }
        (void)mnemonic_text(renderer, font, bar->menus[i].label, x + 8, 1, ink, 0);
        x += w + 16;
    }
    if (bar->active >= 0 && bar->open) {
        const ui_menu *menu = &bar->menus[bar->active];
        SDL_Rect r = popup_rect(bar, font);
        int y = r.y + 1;
        fill(renderer, r.x, r.y, r.w, r.h, white);
        frame_rect(renderer, r, black);
        fill(renderer, r.x + 2, r.y + r.h, r.w, 2, black);
        fill(renderer, r.x + r.w, r.y + 2, 2, r.h, black);
        for (i = 0; i < menu->count; ++i) {
            const ui_menu_item *item = &menu->items[i];
            SDL_Color ink = item->disabled ? gray : black;
            if (!item->label) {
                fill(renderer, r.x + 1, y + 3, r.w - 2, 1, black);
                y += 8;
                continue;
            }
            if (i == bar->item) {
                fill(renderer, r.x + 1, y, r.w - 2, 16, navy);
                if (!item->disabled) ink = white;
            }
            if (item->checked) {
                /* Windows 3.1 check mark. */
                int k;
                for (k = 0; k < 3; ++k) fill(renderer, r.x + 5 + k, y + 7 + k, 1, 2, ink);
                for (k = 0; k < 5; ++k) fill(renderer, r.x + 8 + k, y + 9 - k, 1, 2, ink);
            }
            (void)mnemonic_text(renderer, font, item->label, r.x + 18, y, ink, 0);
            {
                const char *tab = strchr(item->label, '\t');
                if (tab) ui_text(renderer, font, tab + 1,
                    r.x + r.w - 10 - ui_text_width(font, tab + 1), y, ink);
            }
            y += 16;
        }
    }
}

static int menu_step(const ui_menu *menu, int from, int step)
{
    int index = from;
    int tries;
    for (tries = 0; tries < menu->count; ++tries) {
        index = (index + step + menu->count) % menu->count;
        if (menu->items[index].label) return index;
    }
    return from;
}

static int menu_choose(ui_menu_bar *bar)
{
    const ui_menu_item *item = &bar->menus[bar->active].items[bar->item];
    if (!item->label || item->disabled) return 0;
    bar->active = -1;
    bar->open = 0;
    return item->id;
}

int ui_menu_event(ui_menu_bar *bar, const SDL_Event *event)
{
    int left = 0;
    int right = 0;
    int up = 0;
    int down = 0;
    int choose = 0;
    int cancel = 0;
    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        if (bar->active < 0) {
            if (key == SDLK_F10 || key == SDLK_LALT || key == SDLK_RALT) {
                bar->active = 0;
                bar->open = 0;
                bar->item = 0;
            }
            return 0;
        }
        left = key == SDLK_LEFT;
        right = key == SDLK_RIGHT;
        up = key == SDLK_UP;
        down = key == SDLK_DOWN;
        choose = key == SDLK_RETURN;
        cancel = key == SDLK_ESCAPE || key == SDLK_F10;
        if (!left && !right && !up && !down && !choose && !cancel) {
            int i;
            if (!bar->open) {
                for (i = 0; i < bar->count; ++i)
                    if (mnemonic_of(bar->menus[i].label) == (char)key) {
                        bar->active = i;
                        bar->open = 1;
                        bar->item = menu_step(&bar->menus[i], -1, 1);
                    }
            } else {
                const ui_menu *menu = &bar->menus[bar->active];
                for (i = 0; i < menu->count; ++i)
                    if (menu->items[i].label && mnemonic_of(menu->items[i].label) == (char)key) {
                        bar->item = i;
                        return menu_choose(bar);
                    }
            }
            return 0;
        }
    } else if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 button = event->cbutton.button;
        if (bar->active < 0) {
            if (button == SDL_CONTROLLER_BUTTON_START) {
                bar->active = 0;
                bar->open = 1;
                bar->item = menu_step(&bar->menus[0], -1, 1);
            }
            return 0;
        }
        left = button == SDL_CONTROLLER_BUTTON_DPAD_LEFT;
        right = button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        up = button == SDL_CONTROLLER_BUTTON_DPAD_UP;
        down = button == SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        choose = button == SDL_CONTROLLER_BUTTON_A;
        cancel = button == SDL_CONTROLLER_BUTTON_B || button == SDL_CONTROLLER_BUTTON_START;
    } else if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        TTF_Font *font = ui_font(UI_FACE_SYSTEM, 10, 0);
        int x = event->button.x;
        int y = event->button.y;
        int i;
        if (y < UI_MENU_BAR_H) {
            for (i = 0; i < bar->count; ++i) {
                int x0 = menu_title_x(bar, i, font);
                int x1 = menu_title_x(bar, i + 1, font);
                if (x >= x0 && x < x1) {
                    if (bar->active == i && bar->open) { bar->active = -1; bar->open = 0; }
                    else {
                        bar->active = i;
                        bar->open = 1;
                        bar->item = -1;
                    }
                    return 0;
                }
            }
            bar->active = -1;
            bar->open = 0;
            return 0;
        }
        if (bar->active >= 0 && bar->open) {
            SDL_Rect r = popup_rect(bar, font);
            if (point_in(r, x, y)) {
                const ui_menu *menu = &bar->menus[bar->active];
                int iy = r.y + 1;
                for (i = 0; i < menu->count; ++i) {
                    int h = menu->items[i].label ? 16 : 8;
                    if (y >= iy && y < iy + h) {
                        bar->item = i;
                        return menu_choose(bar);
                    }
                    iy += h;
                }
                return 0;
            }
            bar->active = -1;
            bar->open = 0;
        }
        return 0;
    } else {
        return 0;
    }
    if (cancel) {
        if (bar->open && event->type == SDL_KEYDOWN) bar->open = 0;
        else { bar->active = -1; bar->open = 0; }
        return 0;
    }
    if (left || right) {
        bar->active = (bar->active + (left ? -1 : 1) + bar->count) % bar->count;
        bar->item = menu_step(&bar->menus[bar->active], -1, 1);
        return 0;
    }
    if (!bar->open) {
        if (down || choose) {
            bar->open = 1;
            bar->item = menu_step(&bar->menus[bar->active], -1, 1);
        }
        return 0;
    }
    if (up || down) {
        bar->item = menu_step(&bar->menus[bar->active], bar->item < 0 ? (up ? 0 : -1) : bar->item,
            up ? -1 : 1);
        return 0;
    }
    if (choose && bar->item >= 0) return menu_choose(bar);
    return 0;
}
