/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#define FRAME 4         /* modal frame: black, 2px active border, black */
#define CAPTION_H 18
#define BUTTON_W 60     /* 40 DLU */
#define BUTTON_H 23     /* 14 DLU */

static const SDL_Color black = {0, 0, 0, 255};
static const SDL_Color white = {255, 255, 255, 255};

/* The 640x363 logical screen is shown 1.5x on the Vita's 960x544 panel.
 * Text is rasterized at panel resolution and placed on whole panel pixels so
 * glyph strokes stay crisp instead of being resampled. */
#define TEXT_SCALE 3 / 2
#define TO_LOGICAL(v) ((v) * 2 / 3)

static int font_height(TTF_Font *font) { return TO_LOGICAL(TTF_FontHeight(font)); }
static int font_line_skip(TTF_Font *font) { return TO_LOGICAL(TTF_FontLineSkip(font)); }
static int font_ascent(TTF_Font *font) { return TO_LOGICAL(TTF_FontAscent(font)); }

static void set_color(SDL_Renderer *renderer, int r, int g, int b)
{
    SDL_SetRenderDrawColor(renderer, (Uint8)r, (Uint8)g, (Uint8)b, 255);
}

static void fill(SDL_Renderer *renderer, int x, int y, int w, int h)
{
    SDL_Rect rect = {x, y, w, h};
    (void)SDL_RenderFillRect(renderer, &rect);
}

static void text_cache_flush(void);

int ui_fonts_open(ui_fonts *fonts, const char *regular, const char *bold)
{
    fonts->dialog = TTF_OpenFont(regular, 11 * TEXT_SCALE);
    fonts->caption = TTF_OpenFont(bold, 11 * TEXT_SCALE);
    if (fonts->dialog) TTF_SetFontHinting(fonts->dialog, TTF_HINTING_MONO);
    if (fonts->caption) TTF_SetFontHinting(fonts->caption, TTF_HINTING_MONO);
    return fonts->dialog && fonts->caption;
}

void ui_fonts_close(ui_fonts *fonts)
{
    text_cache_flush();
    if (fonts->dialog) TTF_CloseFont(fonts->dialog);
    if (fonts->caption) TTF_CloseFont(fonts->caption);
    fonts->dialog = NULL;
    fonts->caption = NULL;
}

SDL_Rect ui_dlu(int x, int y, int cx, int cy)
{
    SDL_Rect rect = {x * 6 / 4, y * 13 / 8, cx * 6 / 4, cy * 13 / 8};
    return rect;
}

/* Rasterized strings are cached; dialogs redraw every frame. */
#define TEXT_CACHE 48

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

static void blit_text(SDL_Renderer *renderer, TTF_Font *font, const char *text,
    int x, int y, SDL_Color color)
{
    Uint32 key = ((Uint32)color.r << 16) | ((Uint32)color.g << 8) | color.b;
    text_entry *slot = &text_cache[0];
    SDL_FRect target;
    int index;
    if (!text[0]) return;
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

static int text_width(TTF_Font *font, const char *text)
{
    int w = 0;
    int h = 0;
    if (!text[0]) return 0;
    (void)TTF_SizeText(font, text, &w, &h);
    w = TO_LOGICAL(w);
    return w;
}

/* Splits text into lines no wider than `width`, like DrawText DT_WORDBREAK.
 * Returns the number of lines written into `lines`. */
static int wrap_text(TTF_Font *font, const char *text, int width,
    char lines[][UI_TEXT_CAPACITY], int max_lines)
{
    int count = 0;
    const char *cursor = text;
    while (*cursor && count < max_lines) {
        char line[UI_TEXT_CAPACITY] = "";
        size_t used = 0U;
        const char *word = cursor;
        while (*word && *word != '\n') {
            const char *end = word;
            char candidate[UI_TEXT_CAPACITY];
            while (*end && *end != ' ' && *end != '\n') ++end;
            while (*end == ' ') ++end;
            if (used + (size_t)(end - word) >= sizeof candidate) break;
            memcpy(candidate, line, used);
            memcpy(candidate + used, word, (size_t)(end - word));
            candidate[used + (size_t)(end - word)] = '\0';
            {
                /* Trailing spaces do not count toward the width. */
                char measured[UI_TEXT_CAPACITY];
                size_t length = strlen(candidate);
                memcpy(measured, candidate, length + 1U);
                while (length > 0U && measured[length - 1U] == ' ') measured[--length] = '\0';
                if (used > 0U && text_width(font, measured) > width) break;
            }
            memcpy(line, candidate, strlen(candidate) + 1U);
            used = strlen(line);
            word = end;
        }
        while (used > 0U && line[used - 1U] == ' ') line[--used] = '\0';
        memcpy(lines[count++], line, used + 1U);
        cursor = word;
        if (*cursor == '\n') ++cursor;
    }
    return count;
}

void ui_draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text,
    SDL_Rect rect, ui_align align, SDL_Color color)
{
    char lines[8][UI_TEXT_CAPACITY];
    int count = wrap_text(font, text, rect.w, lines, 8);
    int line_h = font_line_skip(font);
    int index;
    for (index = 0; index < count; ++index) {
        int w = text_width(font, lines[index]);
        int x = rect.x;
        if (align == UI_CENTER) x = rect.x + (rect.w - w) / 2;
        else if (align == UI_RIGHT) x = rect.x + rect.w - w;
        blit_text(renderer, font, lines[index], x, rect.y + index * line_h, color);
    }
}

static SDL_Rect measure_block(TTF_Font *font, const char *text, int max_width)
{
    char lines[8][UI_TEXT_CAPACITY];
    int count = wrap_text(font, text, max_width, lines, 8);
    SDL_Rect size = {0, 0, 0, count * font_line_skip(font)};
    int index;
    for (index = 0; index < count; ++index) {
        int w = text_width(font, lines[index]);
        if (w > size.w) size.w = w;
    }
    return size;
}

void ui_dialog_begin(ui_dialog *dialog, const char *caption, int client_w, int client_h)
{
    int outer_w = client_w + 2 * FRAME;
    int outer_h = client_h + 2 * FRAME + CAPTION_H + 1;
    memset(dialog, 0, sizeof *dialog);
    dialog->open = 1;
    (void)snprintf(dialog->caption, sizeof dialog->caption, "%s", caption);
    /* CENTERHWND over the main window, which fills the logical screen. */
    dialog->client.x = (640 - outer_w) / 2 + FRAME;
    dialog->client.y = (363 - outer_h) / 2 + FRAME + CAPTION_H + 1;
    dialog->client.w = client_w;
    dialog->client.h = client_h;
    dialog->default_button = -1;
}

ui_static *ui_dialog_static(ui_dialog *dialog, SDL_Rect rect, ui_align align, const char *text)
{
    ui_static *item;
    if (dialog->static_count >= UI_MAX_STATICS) return NULL;
    item = &dialog->statics[dialog->static_count++];
    item->rect = rect;
    item->align = align;
    (void)snprintf(item->text, sizeof item->text, "%s", text);
    return item;
}

void ui_dialog_button(ui_dialog *dialog, SDL_Rect rect, int id, const char *label, int is_default)
{
    ui_button *button;
    if (dialog->button_count >= UI_MAX_BUTTONS) return;
    button = &dialog->buttons[dialog->button_count];
    button->rect = rect;
    button->id = id;
    (void)snprintf(button->label, sizeof button->label, "%s", label);
    if (is_default) {
        dialog->default_button = dialog->button_count;
        dialog->focus = dialog->button_count;
    }
    ++dialog->button_count;
}

void ui_message_box(ui_dialog *dialog, const ui_fonts *fonts, const char *caption,
    const char *text, int yes_no, int icon_question)
{
    const int margin = 12;
    const int icon_space = icon_question ? 32 + 16 : 0;
    SDL_Rect block = measure_block(fonts->dialog, text, 320);
    int buttons_w = yes_no ? 2 * BUTTON_W + 12 : BUTTON_W;
    int content_w = icon_space + block.w;
    int client_w = (content_w > buttons_w ? content_w : buttons_w) + 2 * 16;
    int text_h = block.h > (icon_question ? 32 : 0) ? block.h : 32;
    int client_h;
    int button_y;
    int x;
    if (!icon_question) text_h = block.h;
    client_h = margin + text_h + margin + BUTTON_H + 10;
    ui_dialog_begin(dialog, caption, client_w, client_h);
    {
        SDL_Rect rect = {16 + icon_space, margin
            + (icon_question && block.h < 32 ? (32 - block.h) / 2 : 0), block.w, block.h};
        ui_dialog_static(dialog, rect, UI_LEFT, text);
    }
    dialog->icon_question = icon_question;
    button_y = margin + text_h + margin;
    x = (client_w - buttons_w) / 2;
    if (yes_no) {
        SDL_Rect yes = {x, button_y, BUTTON_W, BUTTON_H};
        SDL_Rect no = {x + BUTTON_W + 12, button_y, BUTTON_W, BUTTON_H};
        ui_dialog_button(dialog, yes, 6, "&Yes", 1);
        ui_dialog_button(dialog, no, 7, "&No", 0);
        dialog->cancel_id = 0;
    } else {
        SDL_Rect ok = {x, button_y, BUTTON_W, BUTTON_H};
        ui_dialog_button(dialog, ok, 1, "OK", 1);
        dialog->cancel_id = 1;
    }
}

static int mnemonic_match(const ui_button *button, SDL_Keycode key)
{
    const char *amp = strchr(button->label, '&');
    char c;
    if (!amp || !amp[1]) return 0;
    c = amp[1];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return key == (SDL_Keycode)c;
}

int ui_dialog_event(ui_dialog *dialog, const SDL_Event *event)
{
    int index;
    if (!dialog->open || dialog->button_count == 0) return 0;
    if (event->type == SDL_KEYDOWN) {
        SDL_Keycode key = event->key.keysym.sym;
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
            return dialog->buttons[dialog->focus].id;
        if (key == SDLK_ESCAPE) return dialog->cancel_id;
        if (key == SDLK_TAB || key == SDLK_RIGHT || key == SDLK_DOWN)
            dialog->focus = (dialog->focus + 1) % dialog->button_count;
        if (key == SDLK_LEFT || key == SDLK_UP)
            dialog->focus = (dialog->focus + dialog->button_count - 1) % dialog->button_count;
        for (index = 0; index < dialog->button_count; ++index)
            if (mnemonic_match(&dialog->buttons[index], key)) return dialog->buttons[index].id;
    }
    if (event->type == SDL_CONTROLLERBUTTONDOWN) {
        Uint8 button = event->cbutton.button;
        if (button == SDL_CONTROLLER_BUTTON_A) return dialog->buttons[dialog->focus].id;
        if (button == SDL_CONTROLLER_BUTTON_B) return dialog->cancel_id;
        if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || button == SDL_CONTROLLER_BUTTON_DPAD_DOWN)
            dialog->focus = (dialog->focus + 1) % dialog->button_count;
        if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT || button == SDL_CONTROLLER_BUTTON_DPAD_UP)
            dialog->focus = (dialog->focus + dialog->button_count - 1) % dialog->button_count;
    }
    return 0;
}

static void draw_focus_rect(SDL_Renderer *renderer, SDL_Rect rect)
{
    int i;
    set_color(renderer, 0, 0, 0);
    for (i = 0; i < rect.w; i += 2) {
        (void)SDL_RenderDrawPoint(renderer, rect.x + i, rect.y);
        (void)SDL_RenderDrawPoint(renderer, rect.x + i, rect.y + rect.h - 1);
    }
    for (i = 0; i < rect.h; i += 2) {
        (void)SDL_RenderDrawPoint(renderer, rect.x, rect.y + i);
        (void)SDL_RenderDrawPoint(renderer, rect.x + rect.w - 1, rect.y + i);
    }
}

/* Windows 3.1 push button: rounded black outline (thicker when default),
 * 2px white highlight and 2px gray shadow around a gray face. */
static void draw_button(SDL_Renderer *renderer, const ui_fonts *fonts, const ui_button *button,
    SDL_Rect r, int is_default, int focused)
{
    char label[24];
    int underline = -1;
    int w;
    int tx;
    int ty;
    int line_h = font_height(fonts->dialog);
    size_t in = 0U;
    size_t out = 0U;
    int border = is_default ? 2 : 1;
    while (button->label[in] && out + 1U < sizeof label) {
        if (button->label[in] == '&' && button->label[in + 1]) {
            underline = (int)out;
            ++in;
        }
        label[out++] = button->label[in++];
    }
    label[out] = '\0';
    set_color(renderer, 0, 0, 0);
    fill(renderer, r.x + 1, r.y, r.w - 2, r.h);
    fill(renderer, r.x, r.y + 1, r.w, r.h - 2);
    set_color(renderer, 192, 192, 192);
    fill(renderer, r.x + border, r.y + border, r.w - 2 * border, r.h - 2 * border);
    set_color(renderer, 255, 255, 255);
    fill(renderer, r.x + border, r.y + border, r.w - 2 * border - 1, 2);
    fill(renderer, r.x + border, r.y + border, 2, r.h - 2 * border - 1);
    set_color(renderer, 128, 128, 128);
    fill(renderer, r.x + border, r.y + r.h - border - 2, r.w - 2 * border, 2);
    fill(renderer, r.x + r.w - border - 2, r.y + border, 2, r.h - 2 * border);
    w = text_width(fonts->dialog, label);
    tx = r.x + (r.w - w) / 2;
    ty = r.y + (r.h - line_h) / 2;
    blit_text(renderer, fonts->dialog, label, tx, ty, black);
    if (underline >= 0) {
        char prefix[24];
        char one[2] = {label[underline], '\0'};
        int ux;
        memcpy(prefix, label, (size_t)underline);
        prefix[underline] = '\0';
        ux = tx + text_width(fonts->dialog, prefix);
        set_color(renderer, 0, 0, 0);
        fill(renderer, ux, ty + font_ascent(fonts->dialog) + 1,
            text_width(fonts->dialog, one), 1);
    }
    if (focused) {
        SDL_Rect focus = {tx - 2, ty - 1, w + 4, line_h + 2};
        draw_focus_rect(renderer, focus);
    }
}

static void draw_question_icon(SDL_Renderer *renderer, const ui_fonts *fonts, int x, int y)
{
    /* Approximation of the Windows 3.1 question icon; the system icon is not
     * part of the reference archive. */
    int dy;
    for (dy = -13; dy <= 13; ++dy) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= 13 * 13) ++dx;
        set_color(renderer, 0, 0, 0);
        fill(renderer, x + 16 - dx - 1, y + 14 + dy, 2 * dx + 2, 1);
        set_color(renderer, 255, 255, 255);
        if (dx > 0) fill(renderer, x + 16 - dx, y + 14 + dy, 2 * dx, 1);
    }
    {
        SDL_Color blue = {0, 0, 128, 255};
        int w = text_width(fonts->caption, "?");
        blit_text(renderer, fonts->caption, "?", x + 16 - w / 2,
            y + 14 - font_height(fonts->caption) / 2, blue);
    }
}

void ui_dialog_draw(SDL_Renderer *renderer, const ui_fonts *fonts, const ui_dialog *dialog)
{
    SDL_Rect c = dialog->client;
    int outer_x = c.x - FRAME;
    int outer_y = c.y - FRAME - CAPTION_H - 1;
    int outer_w = c.w + 2 * FRAME;
    int outer_h = c.h + 2 * FRAME + CAPTION_H + 1;
    int index;
    if (!dialog->open) return;
    /* Modal frame. */
    set_color(renderer, 0, 0, 0);
    fill(renderer, outer_x, outer_y, outer_w, outer_h);
    set_color(renderer, 0, 0, 128);
    fill(renderer, outer_x + 1, outer_y + 1, outer_w - 2, outer_h - 2);
    set_color(renderer, 0, 0, 0);
    fill(renderer, outer_x + FRAME - 1, outer_y + FRAME - 1,
        outer_w - 2 * FRAME + 2, outer_h - 2 * FRAME + 2);
    /* Caption bar with system menu box. */
    set_color(renderer, 0, 0, 128);
    fill(renderer, c.x, outer_y + FRAME, c.w, CAPTION_H);
    set_color(renderer, 192, 192, 192);
    fill(renderer, c.x, outer_y + FRAME, CAPTION_H, CAPTION_H);
    set_color(renderer, 0, 0, 0);
    fill(renderer, c.x + CAPTION_H, outer_y + FRAME, 1, CAPTION_H);
    set_color(renderer, 128, 128, 128);
    fill(renderer, c.x + 4, outer_y + FRAME + 8, 11, 3);
    set_color(renderer, 255, 255, 255);
    fill(renderer, c.x + 3, outer_y + FRAME + 7, 11, 3);
    {
        SDL_Rect title = {c.x + CAPTION_H + 1, outer_y + FRAME
            + (CAPTION_H - font_height(fonts->caption)) / 2,
            c.w - CAPTION_H - 1, CAPTION_H};
        ui_draw_text(renderer, fonts->caption, dialog->caption, title, UI_CENTER, white);
    }
    set_color(renderer, 0, 0, 0);
    fill(renderer, c.x, c.y - 1, c.w, 1);
    /* Client: GRAYDLGPROC paints light gray. */
    set_color(renderer, 192, 192, 192);
    fill(renderer, c.x, c.y, c.w, c.h);
    if (dialog->icon_question) draw_question_icon(renderer, fonts, c.x + 16, c.y + 12);
    for (index = 0; index < dialog->static_count; ++index) {
        const ui_static *item = &dialog->statics[index];
        SDL_Rect rect = {c.x + item->rect.x, c.y + item->rect.y, item->rect.w, item->rect.h};
        ui_draw_text(renderer, fonts->dialog, item->text, rect, item->align, black);
    }
    for (index = 0; index < dialog->button_count; ++index) {
        const ui_button *button = &dialog->buttons[index];
        SDL_Rect rect = {c.x + button->rect.x, c.y + button->rect.y,
            button->rect.w, button->rect.h};
        draw_button(renderer, fonts, button, rect,
            index == dialog->focus, index == dialog->focus);
    }
}
