/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VCC_UI_H
#define VCC_UI_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/* Windows 3.1 style windowing drawn in the 640x363 logical space that the
 * Vita panel shows at exactly 1.5x. Dialogs follow the CHIPS.EXE and
 * WEP4UTIL.DLL templates; GRAYDLGPROC gives them a light gray client and
 * CENTERHWND centres them over the main window. */

#define UI_SCREEN_W 640
#define UI_SCREEN_H 363
#define UI_MAX_CONTROLS 24
#define UI_TEXT_CAPACITY 192
#define UI_LIST_CAPACITY 160

/* ---- Fonts and text ------------------------------------------------- */

typedef enum ui_face {
    UI_FACE_DIALOG,  /* MS Sans Serif 8 */
    UI_FACE_SYSTEM,  /* System, bold */
    UI_FACE_ARIAL,   /* Arial, sized in points */
    UI_FACE_ARIAL_BOLD
} ui_face;

typedef enum ui_align { UI_LEFT, UI_CENTER, UI_RIGHT } ui_align;

int ui_init(const char *regular_path, const char *bold_path);
void ui_quit(void);
/* `points` at 96 dpi; italic is synthesized. */
TTF_Font *ui_font(ui_face face, int points, int italic);
int ui_text_width(TTF_Font *font, const char *text);
int ui_line_height(TTF_Font *font);
/* Word-wrapped text like DrawText(DT_WORDBREAK); returns the height used. */
int ui_draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text,
    SDL_Rect rect, ui_align align, SDL_Color color);
int ui_measure_text(TTF_Font *font, const char *text, int width, int *out_width);
void ui_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y,
    SDL_Color color);

/* 2:0F06 style bevel drawn outside `rect`: raised draws white top-left. */
void ui_bevel(SDL_Renderer *renderer, SDL_Rect rect, int depth, int raised);
void ui_fill(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color);

/* Text shrunk from `points` until it fits, as the hint and title paint do. */
void ui_draw_fitted(SDL_Renderer *renderer, ui_face face, int italic, int points,
    const char *text, SDL_Rect rect, SDL_Color color);

/* ---- Dialogs -------------------------------------------------------- */

typedef enum ui_kind {
    UI_STATIC,
    UI_IMAGE,
    UI_BUTTON,
    UI_EDIT,
    UI_LIST
} ui_kind;

typedef struct ui_control {
    ui_kind kind;
    int id;
    SDL_Rect rect;            /* client coordinates in pixels */
    ui_align align;
    int is_default;
    int disabled;
    char text[UI_TEXT_CAPACITY];
    int max_length;           /* edit */
    int selected_all;         /* edit */
    SDL_Texture *image;       /* image */
    SDL_Rect image_source;
    /* list */
    char (*items)[64];
    int item_count;
    int selection;
    int top;
} ui_control;

typedef struct ui_dialog {
    int open;
    char caption[64];
    SDL_Rect client;          /* screen rectangle of the client area */
    ui_face face;             /* template font */
    ui_control controls[UI_MAX_CONTROLS];
    int count;
    int focus;                /* index into controls */
    int cancel_id;            /* id sent for Esc/Circle, 0 if none */
    int icon;                 /* message box icon: 0 none, 1 question, 2 hand,
                               * 3 exclamation */
} ui_dialog;

/* Dialog template units to pixels for MS Sans Serif 8 (base units 6x13)
 * or, without DS_SETFONT, the System font (8x16). */
SDL_Rect ui_dlu(int x, int y, int cx, int cy);
SDL_Rect ui_dlu_system(int x, int y, int cx, int cy);

void ui_dialog_begin(ui_dialog *dialog, const char *caption, int client_w, int client_h);
ui_control *ui_add(ui_dialog *dialog, ui_kind kind, int id, SDL_Rect rect, const char *text);
ui_control *ui_find(ui_dialog *dialog, int id);
void ui_set_text(ui_dialog *dialog, int id, const char *text);
void ui_focus(ui_dialog *dialog, int id);

/* MessageBox layouts: MB_OK or MB_YESNO with an optional icon. */
void ui_message_box(ui_dialog *dialog, const char *caption, const char *text,
    int yes_no, int icon);

/* Dialog notifications. */
typedef struct ui_notify {
    int id;
    int code;   /* 0 command, 1 list selection change, 2 list double click */
} ui_notify;

/* Feeds an event to the dialog; returns 1 and fills `notify` when a
 * control reports something. Mouse coordinates must be logical. */
int ui_dialog_event(ui_dialog *dialog, const SDL_Event *event, ui_notify *notify);
void ui_dialog_draw(SDL_Renderer *renderer, const ui_dialog *dialog);

/* ---- Menus ---------------------------------------------------------- */

#define UI_MENU_ITEMS 8

typedef struct ui_menu_item {
    const char *label;   /* '&' mnemonic, '\t' accelerator; NULL separator */
    int id;
    int checked;
    int disabled;
} ui_menu_item;

typedef struct ui_menu {
    const char *label;
    ui_menu_item items[UI_MENU_ITEMS];
    int count;
} ui_menu;

typedef struct ui_menu_bar {
    ui_menu menus[4];
    int count;
    int active;          /* -1 closed, else highlighted top-level menu */
    int open;            /* popup shown */
    int item;            /* highlighted item */
} ui_menu_bar;

#define UI_MENU_BAR_H 19

void ui_menu_draw(SDL_Renderer *renderer, const ui_menu_bar *bar);
/* Returns a command id when one is chosen, else 0. */
int ui_menu_event(ui_menu_bar *bar, const SDL_Event *event);

#endif
