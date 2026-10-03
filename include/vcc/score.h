/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VCC_SCORE_H
#define VCC_SCORE_H

#include <stdbool.h>
#include <stdint.h>

/* Level Complete dialog headline, chosen by failed attempts (6:04A6). */
typedef enum vcc_complete_title {
    VCC_TITLE_FIRST_TRY,   /* attempts == 0:  "Yowser! First Try!" */
    VCC_TITLE_BIT_BUSTER,  /* attempts 1-2:   "Go Bit Buster!" */
    VCC_TITLE_GOOD_WORK,   /* attempts 3-4:   "Finished! Good Work!" */
    VCC_TITLE_AT_LAST      /* attempts >= 5:  "At last! You did it!" */
} vcc_complete_title;

typedef enum vcc_record_message {
    VCC_RECORD_ESTABLISHED,  /* no prior record (DS:0BB7) */
    VCC_RECORD_BEAT_TIME,    /* time improved (DS:0BED), delta in seconds */
    VCC_RECORD_MORE_POINTS,  /* score improved (DS:0C22), delta in points */
    VCC_RECORD_NONE          /* neither improved; message line is cleared */
} vcc_record_message;

typedef struct vcc_level_record {
    bool present;
    int16_t seconds;  /* best time left */
    int32_t score;    /* best level score */
} vcc_level_record;

typedef struct vcc_completion {
    vcc_complete_title title;
    int16_t time_bonus;
    int32_t level_bonus;
    int32_t level_score;
    vcc_record_message message;
    int32_t message_delta;
    vcc_level_record saved;   /* record written back for this level */
    int32_t total_score;      /* running total after this level */
} vcc_completion;

/* Reconstructs the WM_INITDIALOG handler of DLG_COMPLETE (CHIPS.EXE 6:0422).
 * `previous` is ignored when `level` exceeds `highest_level`, matching the
 * original's skip of the stored-record lookup. */
void vcc_score_completion(vcc_completion *out, uint16_t level,
                          int16_t time_left, int16_t attempts,
                          uint16_t highest_level,
                          const vcc_level_record *previous,
                          int32_t total_score);

/* Failed-attempt bookkeeping from the level loader at 4:0356.
 * `attempts` is state+0xA30; `trouble` is state+0xA32. */
typedef struct vcc_attempts {
    int16_t attempts;
    int16_t trouble;
} vcc_attempts;

/* Called before reloading the same level. Returns true when the loader asks
 * "You seem to be having trouble..." (DS:090C, MB_YESNO|MB_ICONQUESTION). */
bool vcc_attempts_restart(vcc_attempts *state, uint16_t level, uint16_t moves);
/* Answer to that prompt: yes skips to the next level and counts as a fresh
 * level; no clears the trouble counter. */
void vcc_attempts_answer(vcc_attempts *state, bool skip);
/* Loading a different level clears both counters. */
void vcc_attempts_new_level(vcc_attempts *state);

#endif
