/* SPDX-License-Identifier: GPL-3.0-only */
#include "vcc/dat.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void)
{
    uint8_t fixture[69] = {
        0xAC, 0xAA, 0x02, 0x00, 0x01, 0x00,
        0x3D, 0x00,
        0x01, 0x00, 0x64, 0x00, 0x0B, 0x00,
        0x01, 0x00,
        0x0F, 0x00,
        0xFF, 0xFF, 0x01, 0xFF, 0xFF, 0x01,
        0xFF, 0xFF, 0x01, 0xFF, 0xFF, 0x01, 0xFF, 0x04, 0x01,
        0x0F, 0x00,
        0xFF, 0xFF, 0x02, 0xFF, 0xFF, 0x02,
        0xFF, 0xFF, 0x02, 0xFF, 0xFF, 0x02, 0xFF, 0x04, 0x02,
        0x11, 0x00,
        0x03, 0x05, 'T', 'E', 'S', 'T', 0,
        0x06, 0x05, 0xDB, 0xDD, 0xD1, 0xC9, 0,
        0x07, 0x01, 0
    };
    vcc_dat parsed;

    CHECK(vcc_dat_parse(&parsed, fixture, sizeof fixture) == VCC_DAT_OK);
    CHECK(parsed.level_count == 1U);
    CHECK(parsed.levels[0].number == 1U);
    CHECK(parsed.levels[0].time_limit == 100U);
    CHECK(parsed.levels[0].chips_required == 11U);
    CHECK(parsed.levels[0].lower[0] == 1U);
    CHECK(parsed.levels[0].lower[VCC_MAP_CELLS - 1U] == 1U);
    CHECK(parsed.levels[0].upper[0] == 2U);
    CHECK(strcmp(parsed.levels[0].title, "TEST") == 0);
    CHECK(strcmp(parsed.levels[0].password, "BDHP") == 0);

    fixture[0] = 0;
    CHECK(vcc_dat_parse(&parsed, fixture, sizeof fixture) == VCC_DAT_BAD_MAGIC);
    CHECK(vcc_dat_parse(&parsed, fixture, 4U) == VCC_DAT_TRUNCATED);
    puts("DAT parser tests passed");
    return 0;
}
