/* SPDX-License-Identifier: GPL-3.0-only */
#include "vcc/progress.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value); return 1; \
} } while (0)

int main(void)
{
    static vcc_progress p;
    static vcc_progress q;
    static const char ini[] =
        "[Other]\r\nLevel3=XXXX,1,2\r\n"
        "[Chip's Challenge]\r\nHighest Level=4\r\nCurrent Level=3\r\n"
        "Current Score=12345\r\nLevel1=BDHP,93,1430\r\nLevel2=JXMJ\r\n"
        "level3=ECBQ,-1,-1\r\n";
    char buffer[8192];
    size_t length;
    vcc_progress_parse(&p, ini, sizeof ini - 1U);
    CHECK(p.highest_level == 4U && p.current_level == 3U);
    CHECK(p.current_score == 12345);
    CHECK(strcmp(p.levels[1].password, "BDHP") == 0);
    CHECK(p.levels[1].record.present && p.levels[1].record.seconds == 93);
    CHECK(p.levels[1].record.score == 1430);
    CHECK(strcmp(p.levels[2].password, "JXMJ") == 0 && !p.levels[2].record.present);
    CHECK(strcmp(p.levels[3].password, "ECBQ") == 0 && !p.levels[3].record.present);
    length = vcc_progress_format(&p, buffer, sizeof buffer);
    CHECK(length < sizeof buffer);
    CHECK(strstr(buffer, "Level1=BDHP,93,1430\r\n") != NULL);
    CHECK(strstr(buffer, "Level2=JXMJ\r\n") != NULL);
    vcc_progress_parse(&q, buffer, length);
    CHECK(memcmp(&p, &q, sizeof p) == 0);
    puts("progress tests passed");
    return 0;
}
