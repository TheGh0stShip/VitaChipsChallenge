/* SPDX-License-Identifier: GPL-3.0-only */
#include "vcc/score.h"

#include <stdio.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); return 1; \
} } while (0)

int main(void)
{
    vcc_completion c;
    vcc_level_record prev;

    vcc_score_completion(&c, 1U, 50, 0, 0U, NULL, 0);
    CHECK(c.title == VCC_TITLE_FIRST_TRY);
    CHECK(c.time_bonus == 500);
    CHECK(c.level_bonus == 500);
    CHECK(c.level_score == 1000);
    CHECK(c.message == VCC_RECORD_ESTABLISHED);
    CHECK(c.total_score == 1000);

    /* Level 1 drops below 500 after one cut and stops there. */
    vcc_score_completion(&c, 1U, 0, 9, 0U, NULL, 0);
    CHECK(c.title == VCC_TITLE_AT_LAST);
    CHECK(c.level_bonus == 400);

    /* Level 3: 1500 -> 1200 -> 960 -> 768 -> 614 -> 491 (stop). */
    vcc_score_completion(&c, 3U, 0, 20, 0U, NULL, 0);
    CHECK(c.level_bonus == 491);
    vcc_score_completion(&c, 3U, 0, 2, 0U, NULL, 0);
    CHECK(c.title == VCC_TITLE_BIT_BUSTER);
    CHECK(c.level_bonus == 960);
    vcc_score_completion(&c, 3U, 0, 3, 0U, NULL, 0);
    CHECK(c.title == VCC_TITLE_GOOD_WORK);

    prev.present = true;
    prev.seconds = 40;
    prev.score = 5000;
    vcc_score_completion(&c, 5U, 45, 0, 5U, &prev, 10000);
    CHECK(c.level_score == 2950);
    CHECK(c.message == VCC_RECORD_BEAT_TIME);
    CHECK(c.message_delta == 5);
    CHECK(c.saved.seconds == 45 && c.saved.score == 5000);
    CHECK(c.total_score == 10000);

    prev.seconds = 50;
    prev.score = 2000;
    vcc_score_completion(&c, 5U, 45, 0, 5U, &prev, 10000);
    CHECK(c.message == VCC_RECORD_MORE_POINTS);
    CHECK(c.message_delta == 950);
    CHECK(c.saved.seconds == 50 && c.saved.score == 2950);
    CHECK(c.total_score == 10950);

    prev.score = 9000;
    vcc_score_completion(&c, 5U, 45, 0, 5U, &prev, 10000);
    CHECK(c.message == VCC_RECORD_NONE);
    CHECK(c.total_score == 10000);

    /* Beyond the highest level, stored records are ignored. */
    vcc_score_completion(&c, 6U, 45, 0, 5U, &prev, 10000);
    CHECK(c.message == VCC_RECORD_ESTABLISHED);

    {
        vcc_attempts a = {0, 0};
        int i;
        for (i = 0; i < 9; ++i)
            CHECK(!vcc_attempts_restart(&a, 5U, 31U));
        CHECK(!vcc_attempts_restart(&a, 5U, 30U));
        CHECK(vcc_attempts_restart(&a, 5U, 31U));
        CHECK(a.attempts == 11);
        vcc_attempts_answer(&a, false);
        CHECK(a.trouble == 0 && a.attempts == 11);
        for (i = 0; i < 20; ++i)
            CHECK(!vcc_attempts_restart(&a, 144U, 500U));
        vcc_attempts_answer(&a, true);
        CHECK(a.attempts == 0);
    }

    puts("score tests passed");
    return 0;
}
