/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VCC_PROGRESS_H
#define VCC_PROGRESS_H

#include "vcc/dat.h"
#include "vcc/score.h"

#include <stddef.h>
#include <stdint.h>

/* Persistent progress as kept by the Win16 game in ENTPACK.INI, section
 * [Chip's Challenge] (DS:0268, DS:0274). Each LevelN value is written by
 * 2:1C1C as "%s,%d,%li" (password, seconds left, score) or "%s" when only
 * the password is known, and read back by 2:1ADC. */
typedef struct vcc_level_progress {
    char password[VCC_PASSWORD_CAPACITY];
    vcc_level_record record;
} vcc_level_progress;

typedef struct vcc_progress {
    uint16_t highest_level;   /* "Highest Level" */
    uint16_t current_level;   /* "Current Level" */
    int32_t current_score;    /* "Current Score" */
    vcc_level_progress levels[VCC_MAX_LEVELS + 1U];  /* indexed by number */
} vcc_progress;

void vcc_progress_reset(vcc_progress *progress);
/* Parses INI text; unknown sections and keys are ignored. */
void vcc_progress_parse(vcc_progress *progress, const char *text, size_t size);
/* Writes INI text. Returns the length needed, excluding the terminator. */
size_t vcc_progress_format(const vcc_progress *progress, char *out, size_t capacity);

#endif
