/* SPDX-License-Identifier: GPL-3.0-only */
#include "vcc/progress.h"

#include <stdio.h>
#include <string.h>

#define SECTION "Chip's Challenge"

void vcc_progress_reset(vcc_progress *progress)
{
    memset(progress, 0, sizeof *progress);
    progress->highest_level = 1U;
    progress->current_level = 1U;
    progress->color = 1;
    progress->midi_files = 3;
}

void vcc_progress_new_game(vcc_progress *progress)
{
    memset(progress->levels, 0, sizeof progress->levels);
    progress->highest_level = 1U;
    progress->current_score = 0;
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

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

/* Parses an optionally signed decimal integer occupying the whole field
 * (surrounding blanks allowed). Overflow saturates. Returns 0 on garbage. */
static int parse_long(const char *text, size_t length, long *out)
{
    size_t index = 0U;
    int negative = 0;
    int digits = 0;
    long value = 0;
    while (index < length && is_space(text[index]))
        ++index;
    if (index < length && (text[index] == '-' || text[index] == '+'))
        negative = text[index++] == '-';
    while (index < length && text[index] >= '0' && text[index] <= '9') {
        long digit = (long)(text[index++] - '0');
        digits = 1;
        if (value > (2147483647L - digit) / 10L)
            value = 2147483647L;
        else
            value = value * 10L + digit;
    }
    while (index < length && is_space(text[index]))
        ++index;
    if (!digits || index != length)
        return 0;
    *out = negative ? -value : value;
    return 1;
}

static long clamp_long(long value, long low, long high)
{
    return value < low ? low : value > high ? high : value;
}

static int password_char(char c)
{
    return c > ' ' && c < 0x7F && c != ',';
}

/* Mirrors 2:1ADC: the password ends at the first comma, then seconds and
 * score follow. A missing, malformed or negative field reads as no record. */
static void parse_level(vcc_level_progress *level, const char *value, size_t size)
{
    const char *comma = memchr(value, ',', size);
    const char *second;
    size_t length = comma ? (size_t)(comma - value) : size;
    size_t index;
    size_t kept = 0U;
    long seconds;
    long score;
    memset(level, 0, sizeof *level);
    for (index = 0U; index < length && kept + 1U < VCC_PASSWORD_CAPACITY; ++index)
        if (password_char(value[index]))
            level->password[kept++] = value[index];
    level->password[kept] = '\0';
    if (!comma)
        return;
    second = memchr(comma + 1, ',', (size_t)(value + size - comma - 1));
    if (!second)
        return;
    if (!parse_long(comma + 1, (size_t)(second - comma - 1), &seconds)
        || !parse_long(second + 1, (size_t)(value + size - second - 1), &score))
        return;
    if (seconds < 0 || score < 0 || seconds > 32767L)
        return;
    level->record.present = true;
    level->record.seconds = (int16_t)seconds;
    level->record.score = (int32_t)score;
}

enum {
    SEEN_HIGHEST = 1 << 0,
    SEEN_CURRENT = 1 << 1,
    SEEN_SCORE = 1 << 2,
    SEEN_MIDI = 1 << 3,
    SEEN_SOUNDS = 1 << 4,
    SEEN_COLOR = 1 << 5,
    SEEN_MIDI_FILES = 1 << 6
};

/* Claims a key once: like GetPrivateProfileString, the first occurrence
 * of a duplicated key wins. */
static int claim(unsigned *seen, unsigned bit)
{
    if (*seen & bit)
        return 0;
    *seen |= bit;
    return 1;
}

static int16_t parse_option(const char *value, size_t length, int16_t fallback)
{
    long number;
    if (!parse_long(value, length, &number))
        return fallback;
    return (int16_t)clamp_long(number, -32768L, 32767L);
}

void vcc_progress_parse(vcc_progress *progress, const char *text, size_t size)
{
    const char *end;
    int in_section = 0;
    unsigned seen = 0U;
    unsigned char level_seen[VCC_MAX_LEVELS + 1U];
    vcc_progress_reset(progress);
    if (!text)
        return;
    end = text + size;
    memset(level_seen, 0, sizeof level_seen);
    while (text < end) {
        const char *line_end = memchr(text, '\n', (size_t)(end - text));
        const char *next;
        const char *equals;
        const char *key;
        const char *key_end;
        const char *value;
        const char *value_end;
        size_t key_length;
        size_t value_length;
        long number;
        if (!line_end)
            line_end = end;
        next = line_end < end ? line_end + 1 : end;
        while (text < line_end && is_space(*text))
            ++text;
        while (line_end > text && is_space(line_end[-1]))
            --line_end;
        if (text < line_end && *text == '[') {
            const char *close = memchr(text, ']', (size_t)(line_end - text));
            const char *name = text + 1;
            const char *name_end = close;
            if (close) {
                while (name < name_end && is_space(*name)) ++name;
                while (name_end > name && is_space(name_end[-1])) --name_end;
            }
            in_section = close
                && key_equals(name, (size_t)(name_end - name), SECTION);
            text = next;
            continue;
        }
        if (!in_section || text >= line_end || *text == ';'
            || (equals = memchr(text, '=', (size_t)(line_end - text))) == NULL) {
            text = next;
            continue;
        }
        key = text;
        key_end = equals;
        while (key_end > key && is_space(key_end[-1]))
            --key_end;
        key_length = (size_t)(key_end - key);
        value = equals + 1;
        value_end = line_end;
        while (value < value_end && is_space(*value))
            ++value;
        value_length = (size_t)(value_end - value);

        if (key_equals(key, key_length, "Highest Level")) {
            if (claim(&seen, SEEN_HIGHEST) && parse_long(value, value_length, &number))
                progress->highest_level =
                    (uint16_t)clamp_long(number, 1L, (long)VCC_MAX_LEVELS);
        } else if (key_equals(key, key_length, "Current Level")) {
            if (claim(&seen, SEEN_CURRENT) && parse_long(value, value_length, &number))
                progress->current_level =
                    (uint16_t)clamp_long(number, 1L, (long)VCC_MAX_LEVELS);
        } else if (key_equals(key, key_length, "Current Score")) {
            if (claim(&seen, SEEN_SCORE) && parse_long(value, value_length, &number))
                progress->current_score = (int32_t)clamp_long(number, 0L, 2147483647L);
        } else if (key_equals(key, key_length, "MIDI")) {
            if (claim(&seen, SEEN_MIDI))
                progress->music = parse_option(value, value_length, progress->music);
        } else if (key_equals(key, key_length, "Sounds")) {
            if (claim(&seen, SEEN_SOUNDS))
                progress->sounds = parse_option(value, value_length, progress->sounds);
        } else if (key_equals(key, key_length, "Color")) {
            if (claim(&seen, SEEN_COLOR))
                progress->color = parse_option(value, value_length, progress->color);
        } else if (key_equals(key, key_length, "Number of Midi Files")) {
            if (claim(&seen, SEEN_MIDI_FILES))
                progress->midi_files =
                    parse_option(value, value_length, progress->midi_files);
        } else if (key_length > 5U && key_equals(key, 5U, "Level")) {
            size_t index;
            unsigned long level = 0UL;
            int valid = key_length - 5U <= 3U;
            for (index = 5U; valid && index < key_length; ++index) {
                if (key[index] < '0' || key[index] > '9')
                    valid = 0;
                else
                    level = level * 10UL + (unsigned long)(key[index] - '0');
            }
            if (valid && level >= 1UL && level <= VCC_MAX_LEVELS && !level_seen[level]) {
                level_seen[level] = 1U;
                parse_level(&progress->levels[level], value, value_length);
            }
        }
        text = next;
    }
}

size_t vcc_progress_format(const vcc_progress *progress, char *out, size_t capacity)
{
    size_t used = 0U;
    unsigned level;
    if (!out)
        capacity = 0U;
    if (capacity > 0U)
        out[0] = '\0';
#define EMIT(...) do { \
    int n = used < capacity \
        ? snprintf(out + used, capacity - used, __VA_ARGS__) \
        : snprintf(NULL, 0U, __VA_ARGS__); \
    if (n > 0) used += (size_t)n; \
} while (0)
    EMIT("[" SECTION "]\r\n");
    EMIT("Highest Level=%u\r\n", (unsigned)progress->highest_level);
    EMIT("Current Level=%u\r\n", (unsigned)progress->current_level);
    EMIT("Current Score=%ld\r\n", (long)progress->current_score);
    EMIT("MIDI=%d\r\n", (int)progress->music);
    EMIT("Sounds=%d\r\n", (int)progress->sounds);
    EMIT("Color=%d\r\n", (int)progress->color);
    EMIT("Number of Midi Files=%d\r\n", (int)progress->midi_files);
    for (level = 1U; level <= VCC_MAX_LEVELS; ++level) {
        const vcc_level_progress *entry = &progress->levels[level];
        char password[VCC_PASSWORD_CAPACITY];
        size_t index;
        size_t kept = 0U;
        for (index = 0U; index + 1U < VCC_PASSWORD_CAPACITY
             && entry->password[index] != '\0'; ++index)
            if (password_char(entry->password[index]))
                password[kept++] = entry->password[index];
        password[kept] = '\0';
        if (entry->record.present)
            EMIT("Level%u=%s,%d,%ld\r\n", level, password,
                entry->record.seconds < 0 ? 0 : (int)entry->record.seconds,
                entry->record.score < 0 ? 0L : (long)entry->record.score);
        else if (password[0] != '\0')
            EMIT("Level%u=%s\r\n", level, password);
    }
#undef EMIT
    return used;
}
