/* SPDX-License-Identifier: GPL-3.0-only */
#include "help.h"

#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RECORDS 4096
#define MAX_TOPICS 64
#define MAX_PIECES 2048
#define MAX_IMAGES 64
#define MAX_KEYWORDS 256
#define HISTORY 32

typedef struct record {
    char type;
    int value[6];
    char *text;
} record;

typedef struct help_font {
    int attributes;
    int half_points;
    SDL_Color color;
} help_font;

typedef struct piece {
    SDL_Rect rect;
    TTF_Font *font;
    SDL_Color color;
    const char *text;      /* points into the text pool */
    SDL_Texture *image;
    int link;              /* 0 none, 1 jump, 2 popup */
    int target;
    int underline;
} piece;

typedef struct image {
    char name[32];
    SDL_Texture *texture;
    int w;
    int h;
} image;

typedef struct keyword {
    char *word;
    int topic;
} help_keyword;

static struct {
    char *data;
    record records[MAX_RECORDS];
    int record_count;
    int topic_start[MAX_TOPICS];
    char *topic_title[MAX_TOPICS];
    int topic_order[MAX_TOPICS];
    int topic_count;
    help_font fonts[16];
    image images[MAX_IMAGES];
    int image_count;
    help_keyword keywords[MAX_KEYWORDS];
    int keyword_count;
    char directory[128];
    int loaded;
} help;

static struct {
    piece pieces[MAX_PIECES];
    int count;
    int height;
    char pool[32768];
    size_t pool_used;
} layout;

static const SDL_Color black = {0, 0, 0, 255};
static const SDL_Color white = {255, 255, 255, 255};
static const SDL_Color gray = {128, 128, 128, 255};
static const SDL_Color light = {192, 192, 192, 255};
static const SDL_Color navy = {0, 0, 128, 255};
static const SDL_Color green = {0, 128, 0, 255};

/* Window geometry in logical pixels. */
static const SDL_Rect window = {12, 22, 616, 336};
#define CAPTION_H 18
#define BUTTONS_H 28

static SDL_Rect text_area(void)
{
    SDL_Rect r = {window.x + 4, window.y + 4 + CAPTION_H + 1 + BUTTONS_H,
        window.w - 8 - 17, window.h - 8 - CAPTION_H - 1 - BUTTONS_H};
    return r;
}

static int split_fields(char *line, char **fields, int max)
{
    int count = 0;
    while (count < max) {
        char *tab = strchr(line, '\t');
        fields[count++] = line;
        if (!tab) break;
        *tab = '\0';
        line = tab + 1;
    }
    return count;
}

int help_load(SDL_Renderer *renderer, const char *directory)
{
    char path[192];
    FILE *file;
    long size;
    char *line;
    (void)renderer;
    help_free();
    (void)snprintf(help.directory, sizeof help.directory, "%s", directory);
    (void)snprintf(path, sizeof path, "%s/help.txt", directory);
    file = fopen(path, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    help.data = malloc((size_t)size + 1U);
    if (!help.data || fread(help.data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(help.data);
        help.data = NULL;
        return 0;
    }
    fclose(file);
    help.data[size] = '\0';
    line = help.data;
    while (line && *line) {
        char *end = strchr(line, '\n');
        char *fields[8];
        int count;
        if (end) *end = '\0';
        count = split_fields(line, fields, 8);
        switch (fields[0][0]) {
        case 'N':
            if (count >= 8) {
                int n = atoi(fields[1]) & 15;
                help.fonts[n].attributes = atoi(fields[2]);
                help.fonts[n].half_points = atoi(fields[3]);
                help.fonts[n].color.r = (Uint8)atoi(fields[5]);
                help.fonts[n].color.g = (Uint8)atoi(fields[6]);
                help.fonts[n].color.b = (Uint8)atoi(fields[7]);
                help.fonts[n].color.a = 255;
            }
            break;
        case 'T':
            if (count >= 3 && help.topic_count < MAX_TOPICS) {
                int number = atoi(fields[1]);
                if (number >= 0 && number < MAX_TOPICS) {
                    help.topic_start[number] = help.record_count;
                    help.topic_title[number] = fields[2];
                    help.topic_order[help.topic_count++] = number;
                }
            }
            break;
        case 'K':
            if (count >= 3 && help.keyword_count < MAX_KEYWORDS) {
                help.keywords[help.keyword_count].word = fields[1];
                help.keywords[help.keyword_count].topic = atoi(fields[2]);
                ++help.keyword_count;
            }
            break;
        default:
            break;
        }
        if (help.record_count < MAX_RECORDS && strchr("TPQFXLAJUEI", fields[0][0])) {
            record *r = &help.records[help.record_count++];
            int index;
            r->type = fields[0][0];
            r->text = count > 1 ? fields[count - 1] : NULL;
            for (index = 0; index < 6; ++index)
                r->value[index] = index + 1 < count ? atoi(fields[index + 1]) : 0;
            if (r->type == 'P' && count > 1) r->value[5] = fields[1][0];  /* align */
            if (r->type == 'P' && count > 6) {
                r->value[0] = atoi(fields[2]);
                r->value[1] = atoi(fields[3]);
                r->value[2] = atoi(fields[4]);
                r->value[3] = atoi(fields[5]);
                r->value[4] = atoi(fields[6]);
            }
            if (r->type == 'I' && count > 2) r->value[5] = fields[1][0];
        }
        line = end ? end + 1 : NULL;
    }
    help.loaded = help.topic_count > 0;
    return help.loaded;
}

void help_free(void)
{
    int index;
    for (index = 0; index < help.image_count; ++index)
        if (help.images[index].texture) SDL_DestroyTexture(help.images[index].texture);
    free(help.data);
    memset(&help, 0, sizeof help);
}

static image *find_image(SDL_Renderer *renderer, const char *name)
{
    int index;
    char path[192];
    SDL_Surface *surface;
    image *img;
    for (index = 0; index < help.image_count; ++index)
        if (strcmp(help.images[index].name, name) == 0) return &help.images[index];
    if (help.image_count >= MAX_IMAGES) return NULL;
    img = &help.images[help.image_count++];
    (void)snprintf(img->name, sizeof img->name, "%s", name);
    (void)snprintf(path, sizeof path, "%s/%s", help.directory, name);
    surface = SDL_LoadBMP(path);
    if (surface) {
        img->texture = SDL_CreateTextureFromSurface(renderer, surface);
        img->w = surface->w;
        img->h = surface->h;
        SDL_FreeSurface(surface);
    }
    return img;
}

static TTF_Font *font_for(int index)
{
    const help_font *f = &help.fonts[index & 15];
    int points = f->half_points / 2;
    if (points < 6) points = 8;
    return ui_font((f->attributes & 1) ? UI_FACE_ARIAL_BOLD : UI_FACE_ARIAL, points,
        (f->attributes & 2) != 0);
}

/* WinHelp paragraph metrics are in half points; 2/3 pixel each at 96 dpi. */
static int px(int half_points)
{
    return half_points * 2 / 3;
}

typedef struct cursor {
    int x;
    int y;
    int line_h;
    int line_start;   /* first piece on the line */
    int left;
    int first;        /* first-line indent of the paragraph */
} cursor;

static void new_line(cursor *c, int minimum)
{
    c->y += c->line_h > minimum ? c->line_h : minimum;
    c->line_h = 0;
    c->x = c->left;
    c->line_start = layout.count;
}

static void add_piece(const piece *p, cursor *c)
{
    if (layout.count >= MAX_PIECES) return;
    layout.pieces[layout.count++] = *p;
    if (p->rect.h > c->line_h) c->line_h = p->rect.h;
}

static const char *pool_copy(const char *text, size_t length)
{
    char *out;
    if (layout.pool_used + length + 1U > sizeof layout.pool) return "";
    out = layout.pool + layout.pool_used;
    memcpy(out, text, length);
    out[length] = '\0';
    layout.pool_used += length + 1U;
    return out;
}

static void lay_out(SDL_Renderer *renderer, int topic, int width)
{
    int index;
    int font = 1;
    int link = 0;
    int target = 0;
    cursor c = {0, 0, 0, 0, 0, 0};
    int tab = 0;
    int below = 0;
    layout.count = 0;
    layout.pool_used = 0U;
    if (topic < 0 || topic >= MAX_TOPICS || !help.topic_title[topic]) return;
    for (index = help.topic_start[topic] + 1; index < help.record_count; ++index) {
        const record *r = &help.records[index];
        if (r->type == 'T') break;
        switch (r->type) {
        case 'P':
            c.left = px(r->value[0]) + 6;
            c.y += px(r->value[2]);
            c.first = px(r->value[1]);
            c.x = c.left + c.first;
            c.line_start = layout.count;
            below = px(r->value[3]);
            tab = r->value[4] ? px(r->value[4]) + 6 : 0;
            break;
        case 'Q':
            new_line(&c, ui_line_height(font_for(font)));
            c.y += below;
            c.x = c.left + c.first;
            break;
        case 'L':
            new_line(&c, ui_line_height(font_for(font)));
            break;
        case 'A':
            if (tab > c.x) c.x = tab;
            else c.x += 24;
            break;
        case 'F':
            font = r->value[0];
            break;
        case 'J':
        case 'U':
            link = r->type == 'J' ? 1 : 2;
            target = r->value[0];
            break;
        case 'E':
            link = 0;
            break;
        case 'I': {
            image *img = find_image(renderer, r->text ? r->text : "");
            piece p;
            memset(&p, 0, sizeof p);
            if (!img || !img->texture) break;
            if (c.x + img->w > width && c.x > c.left) new_line(&c, 0);
            p.rect = (SDL_Rect){c.x, c.y, img->w, img->h};
            p.image = img->texture;
            p.link = link;
            p.target = target;
            add_piece(&p, &c);
            c.x += img->w;
            break;
        }
        case 'X': {
            TTF_Font *f = font_for(font);
            const char *text = r->text ? r->text : "";
            while (*text) {
                const char *end = text;
                char word[128];
                size_t length;
                int w;
                piece p;
                while (*end == ' ') ++end;
                while (*end && *end != ' ') ++end;
                length = (size_t)(end - text);
                if (length >= sizeof word) length = sizeof word - 1U;
                memcpy(word, text, length);
                word[length] = '\0';
                w = ui_text_width(f, word);
                if (c.x + w > width && c.x > c.left) {
                    new_line(&c, ui_line_height(f));
                    while (*text == ' ') { ++text; --length; }
                    memcpy(word, text, length);
                    word[length] = '\0';
                    w = ui_text_width(f, word);
                }
                memset(&p, 0, sizeof p);
                p.rect = (SDL_Rect){c.x, c.y, w, ui_line_height(f)};
                p.font = f;
                p.text = pool_copy(word, length);
                p.color = link ? green : help.fonts[font & 15].color;
                p.link = link;
                p.target = target;
                p.underline = link;
                add_piece(&p, &c);
                c.x += w;
                text = end;
            }
            break;
        }
        default:
            break;
        }
    }
    layout.height = c.y + c.line_h + 8;
}

static void fill(SDL_Renderer *renderer, SDL_Rect r, SDL_Color c)
{
    ui_fill(renderer, r, c);
}

static const char *const button_labels[5] = {"&Contents", "&Search", "&Back", "<<", ">>"};

static SDL_Rect button_rect(int index)
{
    SDL_Rect r = {window.x + 4 + 6 + index * 74, window.y + 4 + CAPTION_H + 1 + 3, 70, 22};
    return r;
}

static void draw_window(SDL_Renderer *renderer, int topic, int scroll, int focus,
    int back_enabled)
{
    TTF_Font *caption = ui_font(UI_FACE_SYSTEM, 10, 0);
    SDL_Rect area = text_area();
    SDL_Rect r;
    int index;
    char title[96];
    /* Sizable frame and caption. */
    fill(renderer, window, black);
    fill(renderer, (SDL_Rect){window.x + 1, window.y + 1, window.w - 2, window.h - 2}, light);
    fill(renderer, (SDL_Rect){window.x + 3, window.y + 3, window.w - 6, window.h - 6}, black);
    r = (SDL_Rect){window.x + 4, window.y + 4, window.w - 8, CAPTION_H};
    fill(renderer, r, navy);
    fill(renderer, (SDL_Rect){r.x, r.y, CAPTION_H, CAPTION_H}, light);
    fill(renderer, (SDL_Rect){r.x + 3, r.y + 7, 11, 3}, white);
    (void)snprintf(title, sizeof title, "Chip's Challenge Help");
    (void)ui_draw_text(renderer, caption, title,
        (SDL_Rect){r.x + CAPTION_H, r.y + 2, r.w - CAPTION_H, CAPTION_H}, UI_CENTER, white);
    /* Button bar. */
    r = (SDL_Rect){window.x + 4, window.y + 4 + CAPTION_H + 1, window.w - 8, BUTTONS_H - 1};
    fill(renderer, r, light);
    fill(renderer, (SDL_Rect){r.x, r.y + r.h, r.w, 1}, black);
    for (index = 0; index < 5; ++index) {
        SDL_Rect b = button_rect(index);
        int disabled = (index == 2 && !back_enabled);
        char label[16];
        size_t in = 0U;
        size_t out = 0U;
        fill(renderer, (SDL_Rect){b.x + 1, b.y, b.w - 2, b.h}, black);
        fill(renderer, (SDL_Rect){b.x, b.y + 1, b.w, b.h - 2}, black);
        fill(renderer, (SDL_Rect){b.x + 1, b.y + 1, b.w - 2, b.h - 2}, light);
        fill(renderer, (SDL_Rect){b.x + 1, b.y + 1, b.w - 3, 2}, white);
        fill(renderer, (SDL_Rect){b.x + 1, b.y + 1, 2, b.h - 3}, white);
        fill(renderer, (SDL_Rect){b.x + 1, b.y + b.h - 3, b.w - 2, 2}, gray);
        fill(renderer, (SDL_Rect){b.x + b.w - 3, b.y + 1, 2, b.h - 2}, gray);
        while (button_labels[index][in] && out + 1U < sizeof label) {
            if (button_labels[index][in] == '&') { ++in; continue; }
            label[out++] = button_labels[index][in++];
        }
        label[out] = '\0';
        ui_text(renderer, caption, label, b.x + (b.w - ui_text_width(caption, label)) / 2,
            b.y + 3, disabled ? gray : black);
    }
    /* Topic text on white with a vertical scroll bar. */
    fill(renderer, (SDL_Rect){area.x, area.y, area.w + 17, area.h}, white);
    (void)SDL_RenderSetClipRect(renderer, &area);
    for (index = 0; index < layout.count; ++index) {
        const piece *p = &layout.pieces[index];
        SDL_Rect at = {area.x + p->rect.x, area.y + p->rect.y - scroll, p->rect.w, p->rect.h};
        if (at.y + at.h < area.y || at.y > area.y + area.h) continue;
        if (p->image) (void)SDL_RenderCopy(renderer, p->image, NULL, &at);
        else ui_text(renderer, p->font, p->text, at.x, at.y, p->color);
        if (p->underline == 1) fill(renderer, (SDL_Rect){at.x, at.y + at.h - 2, at.w, 1}, green);
        if (p->underline == 2) {
            int x;
            for (x = 0; x < at.w; x += 2) fill(renderer, (SDL_Rect){at.x + x, at.y + at.h - 2, 1, 1}, green);
        }
        if (p->link && focus >= 0 && layout.pieces[focus].target == p->target
            && index >= focus && (index == focus || layout.pieces[index - 1].link))
            fill(renderer, (SDL_Rect){at.x, at.y, at.w, 1}, black);
    }
    (void)SDL_RenderSetClipRect(renderer, NULL);
    {
        SDL_Rect bar = {area.x + area.w, area.y, 17, area.h};
        fill(renderer, bar, light);
        fill(renderer, (SDL_Rect){bar.x, bar.y, 1, bar.h}, black);
        if (layout.height > area.h) {
            int thumb = bar.y + (bar.h - 17) * scroll / (layout.height - area.h);
            fill(renderer, (SDL_Rect){bar.x + 1, thumb, 16, 17}, white);
            fill(renderer, (SDL_Rect){bar.x + 1, thumb + 16, 16, 1}, black);
        }
    }
    (void)topic;
}

static int keyword_topic(const char *word)
{
    int index;
    for (index = 0; index < help.keyword_count; ++index)
        if (SDL_strcasecmp(help.keywords[index].word, word) == 0) return help.keywords[index].topic;
    return help.topic_count > 0 ? help.topic_order[0] : 0;
}

static int next_link(int from, int step)
{
    int index = from;
    int tries;
    for (tries = 0; tries < layout.count; ++tries) {
        index += step;
        if (index < 0) index = layout.count - 1;
        if (index >= layout.count) index = 0;
        if (layout.pieces[index].link
            && (index == 0 || !layout.pieces[index - 1].link
                || layout.pieces[index - 1].target != layout.pieces[index].target))
            return index;
    }
    return -1;
}

/* A popup topic in a bordered box, closed by any key or click. */
static void show_popup(SDL_Renderer *renderer, int topic, help_backdrop backdrop, void *context)
{
    piece saved[MAX_PIECES];
    int saved_count = layout.count;
    int saved_height = layout.height;
    int done = 0;
    memcpy(saved, layout.pieces, sizeof(piece) * (size_t)layout.count);
    lay_out(renderer, topic, 360);
    while (!done) {
        SDL_Event event;
        SDL_Rect box = {(UI_SCREEN_W - 380) / 2, 120, 380, layout.height + 12};
        int index;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_KEYDOWN || event.type == SDL_CONTROLLERBUTTONDOWN
                || event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_QUIT)
                done = 1;
        }
        backdrop(context);
        fill(renderer, (SDL_Rect){box.x + 3, box.y + 3, box.w, box.h}, black);
        fill(renderer, box, black);
        fill(renderer, (SDL_Rect){box.x + 1, box.y + 1, box.w - 2, box.h - 2}, white);
        for (index = 0; index < layout.count; ++index) {
            const piece *p = &layout.pieces[index];
            SDL_Rect at = {box.x + 4 + p->rect.x, box.y + 4 + p->rect.y, p->rect.w, p->rect.h};
            if (p->image) (void)SDL_RenderCopy(renderer, p->image, NULL, &at);
            else ui_text(renderer, p->font, p->text, at.x, at.y, p->color);
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }
    memcpy(layout.pieces, saved, sizeof(piece) * (size_t)saved_count);
    layout.count = saved_count;
    layout.height = saved_height;
}

/* Search: the keyword list; choosing one shows its first topic. */
static int search(SDL_Renderer *renderer, help_backdrop backdrop, void *context)
{
    static char items[MAX_KEYWORDS][64];
    static int topics[MAX_KEYWORDS];
    int count = 0;
    int index;
    ui_dialog dialog;
    ui_control *list;
    ui_control *button;
    int result = -1;
    for (index = 0; index < help.keyword_count; ++index) {
        if (count > 0 && SDL_strcasecmp(items[count - 1], help.keywords[index].word) == 0)
            continue;
        (void)snprintf(items[count], sizeof items[count], "%s", help.keywords[index].word);
        topics[count++] = help.keywords[index].topic;
    }
    ui_dialog_begin(&dialog, "Search", 300, 220);
    (void)ui_add(&dialog, UI_STATIC, -1, (SDL_Rect){10, 8, 280, 14},
        "Select a word from the list:");
    list = ui_add(&dialog, UI_LIST, 100, (SDL_Rect){10, 26, 280, 150}, "");
    if (list) {
        list->items = items;
        list->item_count = count;
        list->selection = 0;
    }
    button = ui_add(&dialog, UI_BUTTON, 1, (SDL_Rect){70, 186, 70, 23}, "&Show Topic");
    if (button) button->is_default = 1;
    (void)ui_add(&dialog, UI_BUTTON, 2, (SDL_Rect){160, 186, 70, 23}, "Cancel");
    dialog.cancel_id = 2;
    ui_focus(&dialog, 100);
    while (result < 0) {
        SDL_Event event;
        while (result < 0 && SDL_PollEvent(&event)) {
            ui_notify notify;
            if (event.type == SDL_QUIT) result = 0;
            if (ui_dialog_event(&dialog, &event, &notify)) {
                if (notify.id == 2) result = 0;
                else if (notify.id == 1 || (notify.id == 100 && notify.code == 2))
                    result = list && list->selection >= 0 ? topics[list->selection] + 1 : 0;
            }
        }
        backdrop(context);
        ui_dialog_draw(renderer, &dialog);
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }
    return result - 1;
}

static int sequence_index(int topic)
{
    int index;
    for (index = 0; index < help.topic_count; ++index)
        if (help.topic_order[index] == topic) return index;
    return 0;
}

typedef struct view {
    SDL_Renderer *renderer;
    help_backdrop backdrop;
    void *context;
    int topic;
    int scroll;
    int focus;
    int history[HISTORY];
    int depth;
} view;

static void go(view *v, int topic, int remember)
{
    if (remember && v->depth < HISTORY) v->history[v->depth++] = v->topic;
    v->topic = topic;
    v->scroll = 0;
    lay_out(v->renderer, topic, text_area().w - 12);
    v->focus = next_link(-1, 1);
}

static void activate(view *v, int index)
{
    const piece *p;
    if (index < 0 || index >= layout.count) return;
    p = &layout.pieces[index];
    if (p->link == 1) go(v, p->target, 1);
    else if (p->link == 2) show_popup(v->renderer, p->target, v->backdrop, v->context);
}

static void press_button(view *v, int index)
{
    int position = sequence_index(v->topic);
    switch (index) {
    case 0: go(v, keyword_topic("contents"), 1); break;
    case 1: {
        int topic = search(v->renderer, v->backdrop, v->context);
        if (topic >= 0) go(v, topic, 1);
        break;
    }
    case 2: if (v->depth > 0) go(v, v->history[--v->depth], 0); break;
    case 3: if (position > 0) go(v, help.topic_order[position - 1], 1); break;
    case 4: if (position + 1 < help.topic_count && help.topic_title[help.topic_order[position + 1]][0])
                go(v, help.topic_order[position + 1], 1);
        break;
    default: break;
    }
}

void help_run(SDL_Renderer *renderer, const char *keyword, help_backdrop backdrop, void *context)
{
    view v;
    int open = 1;
    SDL_Rect area = text_area();
    if (!help.loaded) return;
    memset(&v, 0, sizeof v);
    v.renderer = renderer;
    v.backdrop = backdrop;
    v.context = context;
    v.topic = -1;
    go(&v, keyword_topic(keyword), 0);
    while (open) {
        SDL_Event event;
        int limit = layout.height > area.h ? layout.height - area.h : 0;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) open = 0;
            if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE: open = 0; break;
                case SDLK_UP: v.scroll -= 16; break;
                case SDLK_DOWN: v.scroll += 16; break;
                case SDLK_PAGEUP: v.scroll -= area.h; break;
                case SDLK_PAGEDOWN: v.scroll += area.h; break;
                case SDLK_TAB:
                    v.focus = next_link(v.focus, (SDL_GetModState() & KMOD_SHIFT) ? -1 : 1);
                    break;
                case SDLK_RETURN: activate(&v, v.focus); break;
                case SDLK_c: press_button(&v, 0); break;
                case SDLK_s: press_button(&v, 1); break;
                case SDLK_b: press_button(&v, 2); break;
                case SDLK_COMMA: press_button(&v, 3); break;
                case SDLK_PERIOD: press_button(&v, 4); break;
                default: break;
                }
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (event.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: v.scroll -= 16; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: v.scroll += 16; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: v.focus = next_link(v.focus, -1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: v.focus = next_link(v.focus, 1); break;
                case SDL_CONTROLLER_BUTTON_A: activate(&v, v.focus); break;
                case SDL_CONTROLLER_BUTTON_B:
                    if (v.depth > 0) press_button(&v, 2);
                    else open = 0;
                    break;
                case SDL_CONTROLLER_BUTTON_X: press_button(&v, 0); break;
                case SDL_CONTROLLER_BUTTON_BACK: press_button(&v, 1); break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: press_button(&v, 3); break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: press_button(&v, 4); break;
                case SDL_CONTROLLER_BUTTON_Y: case SDL_CONTROLLER_BUTTON_START: open = 0; break;
                default: break;
                }
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                int x = event.button.x;
                int y = event.button.y;
                int index;
                for (index = 0; index < 5; ++index) {
                    SDL_Rect b = button_rect(index);
                    if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) press_button(&v, index);
                }
                for (index = 0; index < layout.count; ++index) {
                    const piece *p = &layout.pieces[index];
                    int px0 = area.x + p->rect.x;
                    int py0 = area.y + p->rect.y - v.scroll;
                    if (p->link && x >= px0 && y >= py0 && x < px0 + p->rect.w && y < py0 + p->rect.h
                        && y >= area.y && y < area.y + area.h) {
                        activate(&v, index);
                        break;
                    }
                }
                if (y < window.y || y > window.y + window.h) open = 0;
            }
            if (event.type == SDL_MOUSEWHEEL) v.scroll -= event.wheel.y * 32;
        }
        limit = layout.height > area.h ? layout.height - area.h : 0;
        if (v.scroll > limit) v.scroll = limit;
        if (v.scroll < 0) v.scroll = 0;
        if (v.focus >= 0 && v.focus < layout.count) {
            /* Keep the focused hotspot visible. */
            const piece *p = &layout.pieces[v.focus];
            if (p->rect.y < v.scroll) v.scroll = p->rect.y;
            if (p->rect.y + p->rect.h > v.scroll + area.h) v.scroll = p->rect.y + p->rect.h - area.h;
        }
        backdrop(context);
        draw_window(renderer, v.topic, v.scroll, v.focus, v.depth > 0);
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }
}
