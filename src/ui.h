/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VCC_UI_H
#define VCC_UI_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/* Windows 3.1 style dialogs and message boxes drawn in the logical 640 wide
 * coordinate space. Templates come from the CHIPS.EXE dialog resources;
 * dialogs use WEP4UTIL GRAYDLGPROC (gray client) and CENTERHWND. */

#define UI_MAX_STATICS 8
#define UI_MAX_BUTTONS 3
#define UI_TEXT_CAPACITY 192

typedef enum ui_align { UI_LEFT, UI_CENTER, UI_RIGHT } ui_align;

typedef struct ui_static {
    SDL_Rect rect;   /* client coordinates in pixels */
    ui_align align;
    char text[UI_TEXT_CAPACITY];
} ui_static;

typedef struct ui_button {
    SDL_Rect rect;
    int id;
    char label[24];  /* '&' marks the mnemonic */
} ui_button;

typedef struct ui_dialog {
    int open;
    char caption[64];
    SDL_Rect client;  /* screen rectangle of the client area */
    ui_static statics[UI_MAX_STATICS];
    int static_count;
    ui_button buttons[UI_MAX_BUTTONS];
    int button_count;
    int focus;
    int default_button;
    int cancel_id;    /* button id for Esc/Circle, or 0 when none */
    int icon_question;
} ui_dialog;

typedef struct ui_fonts {
    TTF_Font *dialog;   /* MS Sans Serif 8 stand-in */
    TTF_Font *caption;  /* System (bold) stand-in */
} ui_fonts;

int ui_fonts_open(ui_fonts *fonts, const char *regular, const char *bold);
void ui_fonts_close(ui_fonts *fonts);

/* Dialog template units to pixels for MS Sans Serif 8 (base units 6x13). */
SDL_Rect ui_dlu(int x, int y, int cx, int cy);

void ui_dialog_begin(ui_dialog *dialog, const char *caption, int client_w, int client_h);
ui_static *ui_dialog_static(ui_dialog *dialog, SDL_Rect rect, ui_align align, const char *text);
void ui_dialog_button(ui_dialog *dialog, SDL_Rect rect, int id, const char *label, int is_default);

/* Builds a MessageBox-style dialog (MB_OK or MB_YESNO). */
void ui_message_box(ui_dialog *dialog, const ui_fonts *fonts, const char *caption,
    const char *text, int yes_no, int icon_question);

/* Returns the id of an activated button, or 0. */
int ui_dialog_event(ui_dialog *dialog, const SDL_Event *event);
void ui_dialog_draw(SDL_Renderer *renderer, const ui_fonts *fonts, const ui_dialog *dialog);

/* Draws text clipped to a rectangle; multi-line with word wrapping. */
void ui_draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text,
    SDL_Rect rect, ui_align align, SDL_Color color);

/* Hint window text (2:2CBA): Arial bold italic in cyan, starting at 12 pt
 * and shrinking a point at a time until DT_WORDBREAK text fits, down to 6. */
void ui_draw_hint(SDL_Renderer *renderer, const char *text, SDL_Rect rect);

#endif
