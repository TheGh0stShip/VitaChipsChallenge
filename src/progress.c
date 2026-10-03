/* SPDX-License-Identifier: GPL-3.0-only */
#include "vcc/progress.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTION "Chip's Challenge"

void vcc_progress_reset(vcc_progress *progress)
{
    memset(progress, 0, sizeof *progress);
    progress->highest_level = 1U;
    progress->current_level = 1U;
}

static int key_equals(const char *key, size_t length, const char *name)
{
    size_t index;
    if (strlen(name) != length)
        return 0;
    /* Profile keys are case-insensitive. */
    for (index = 0U; index < length; ++index) {
        char a = key[index];
        char b = name[index];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return 1;
}

/* Mirrors 2:1ADC: the password ends at the first comma, then seconds and
 * score follow. A missing or negative field reads as no record. */
static void parse_level(vcc_level_progress *level, const char *value)
{
    const char *comma = strchr(value, ',');
    size_t length = comma ? (size_t)(comma - value) : strlen(value);
    long seconds;
    long score;
    if (length >= VCC_PASSWORD_CAPACITY)
        length = VCC_PASSWORD_CAPACITY - 1U;
    memcpy(level->password, value, length);
    level->password[length] = '\0';
    level->record.present = false;
    if (!comma)
        return;
    seconds = strtol(comma + 1, NULL, 10);
    comma = strchr(comma + 1, ',');
    if (!comma)
        return;
    score = strtol(comma + 1, NULL, 10);
    if (seconds < 0 || score < 0)
        return;
    level->record.present = true;
    level->record.seconds = (int16_t)seconds;
    level->record.score = (int32_t)score;
}

void vcc_progress_parse(vcc_progress *progress, const char *text, size_t size)
{
    const char *end = text + size;
    int in_section = 0;
    vcc_progress_reset(progress);
    while (text < end) {
        const char *line_end = memchr(text, '\n', (size_t)(end - text));
        const char *equals;
        char value[64];
        size_t value_length;
        if (!line_end)
            line_end = end;
        if (*text == '[') {
            const char *close = memchr(text, ']', (size_t)(line_end - text));
            in_section = close && key_equals(text + 1, (size_t)(close - text - 1), SECTION);
        } else if (in_section
            && (equals = memchr(text, '=', (size_t)(line_end - text))) != NULL) {
            size_t key_length = (size_t)(equals - text);
            value_length = (size_t)(line_end - equals - 1);
            while (value_length > 0U && (equals[value_length] == '\r'))
                --value_length;
            if (value_length >= sizeof value)
                value_length = sizeof value - 1U;
            memcpy(value, equals + 1, value_length);
            value[value_length] = '\0';
            if (key_equals(text, key_length, "Highest Level")) {
                progress->highest_level = (uint16_t)strtoul(value, NULL, 10);
            } else if (key_equals(text, key_length, "Current Level")) {
                progress->current_level = (uint16_t)strtoul(value, NULL, 10);
            } else if (key_equals(text, key_length, "Current Score")) {
                progress->current_score = (int32_t)strtol(value, NULL, 10);
            } else if (key_length > 5U && key_equals(text, 5U, "Level")) {
                char number[8];
                unsigned long level;
                size_t digits = key_length - 5U;
                if (digits < sizeof number) {
                    memcpy(number, text + 5, digits);
                    number[digits] = '\0';
                    level = strtoul(number, NULL, 10);
                    if (level >= 1U && level <= VCC_MAX_LEVELS)
                        parse_level(&progress->levels[level], value);
                }
            }
        }
        text = line_end + 1;
    }
}

size_t vcc_progress_format(const vcc_progress *progress, char *out, size_t capacity)
{
    size_t used = 0U;
    unsigned level;
#define EMIT(...) do { \
    int n = snprintf(out ? out + used : NULL, \
        out && used < capacity ? capacity - used : 0U, __VA_ARGS__); \
    if (n > 0) used += (size_t)n; \
} while (0)
    EMIT("[" SECTION "]\r\n");
    EMIT("Highest Level=%u\r\n", (unsigned)progress->highest_level);
    EMIT("Current Level=%u\r\n", (unsigned)progress->current_level);
    EMIT("Current Score=%ld\r\n", (long)progress->current_score);
    for (level = 1U; level <= VCC_MAX_LEVELS; ++level) {
        const vcc_level_progress *entry = &progress->levels[level];
        if (entry->record.present)
            EMIT("Level%u=%s,%d,%ld\r\n", level, entry->password,
                (int)entry->record.seconds, (long)entry->record.score);
        else if (entry->password[0] != '\0')
            EMIT("Level%u=%s\r\n", level, entry->password);
    }
#undef EMIT
    return used;
}
