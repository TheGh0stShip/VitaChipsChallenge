/* SPDX-License-Identifier: GPL-3.0-only */
#include "vcc/score.h"

#include <stddef.h>

/* Level bonus: 500 * level, cut by a fifth for each failed attempt until it
 * first falls below 500 (6:0441-6:0495). The sub-500 value is kept. */
static int32_t level_bonus(uint16_t level, int16_t attempts)
{
    int32_t bonus = (int32_t)level * 500;
    int16_t i;
    for (i = 0; i < attempts; ++i) {
        bonus = (bonus * 4) / 5;
        if (bonus < 500) {
            break;
        }
    }
    return bonus;
}

static vcc_complete_title title_for(int16_t attempts)
{
    if (attempts == 0) {
        return VCC_TITLE_FIRST_TRY;
    }
    if (attempts < 3) {
        return VCC_TITLE_BIT_BUSTER;
    }
    return attempts < 5 ? VCC_TITLE_GOOD_WORK : VCC_TITLE_AT_LAST;
}

void vcc_score_completion(vcc_completion *out, uint16_t level,
                          int16_t time_left, int16_t attempts,
                          uint16_t highest_level,
                          const vcc_level_record *previous,
                          int32_t total_score)
{
    const vcc_level_record *prev = NULL;

    out->title = title_for(attempts);
    out->time_bonus = (int16_t)(time_left * 10);
    out->level_bonus = level_bonus(level, attempts);
    out->level_score = out->time_bonus + out->level_bonus;

    /* 6:0586: stored records are consulted only up to the highest level,
     * and a negative stored time or score counts as no record. */
    if (level <= highest_level && previous != NULL && previous->present
        && previous->seconds >= 0 && previous->score >= 0) {
        prev = previous;
    }

    if (prev == NULL) {
        out->saved.present = true;
        out->saved.seconds = time_left;
        out->saved.score = out->level_score;
        out->total_score = total_score + out->level_score;
        out->message = VCC_RECORD_ESTABLISHED;
        out->message_delta = 0;
        return;
    }

    out->saved.present = true;
    out->saved.seconds = prev->seconds > time_left ? prev->seconds : time_left;
    out->saved.score = out->level_score > prev->score
        ? out->level_score : prev->score;
    out->total_score = total_score + (out->saved.score - prev->score);

    /* A time improvement is reported in preference to a score one. */
    if (prev->seconds < time_left) {
        out->message = VCC_RECORD_BEAT_TIME;
        out->message_delta = time_left - prev->seconds;
    } else if (out->level_score > prev->score) {
        out->message = VCC_RECORD_MORE_POINTS;
        out->message_delta = out->level_score - prev->score;
    } else {
        out->message = VCC_RECORD_NONE;
        out->message_delta = 0;
    }
}

bool vcc_attempts_restart(vcc_attempts *state, uint16_t level, uint16_t moves)
{
    /* 4:03A7: retries only count toward the prompt after more than 30
     * steps, and never on levels 144 and 149. */
    ++state->attempts;
    if (moves > 30U && level != 144U && level != 149U) {
        ++state->trouble;
        if (state->trouble >= 10)
            return true;
    }
    return false;
}

void vcc_attempts_answer(vcc_attempts *state, bool skip)
{
    if (skip)
        vcc_attempts_new_level(state);
    else
        state->trouble = 0;
}

void vcc_attempts_new_level(vcc_attempts *state)
{
    state->attempts = 0;
    state->trouble = 0;
}
