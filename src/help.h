/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VCC_HELP_H
#define VCC_HELP_H

#include <SDL2/SDL.h>

/* A WinHelp 3.x style viewer for CHIPS.HLP, converted at build time by
 * tools/convert_help.py. The game reaches it through WEP4UTIL WEPHELP with
 * HELP_KEY lookups ("Contents", "How To Play", "Commands", 2:1EBE). */

typedef void (*help_backdrop)(void *context);

int help_load(SDL_Renderer *renderer, const char *directory);
void help_free(void);
/* Runs the help window until it is closed; `keyword` selects the topic. */
void help_run(SDL_Renderer *renderer, const char *keyword, help_backdrop backdrop,
    void *context);

#endif
