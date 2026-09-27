#include "vcc/dat.h"

#include <string.h>

typedef struct cursor {
    const uint8_t *bytes;
    size_t size;
    size_t offset;
} cursor;

static int take_u8(cursor *input, uint8_t *value)
{
    if (input->offset >= input->size)
        return 0;
    *value = input->bytes[input->offset++];
    return 1;
}

static int take_u16(cursor *input, uint16_t *value)
{
    if (input->size - input->offset < 2U)
        return 0;
    *value = (uint16_t)((uint16_t)input->bytes[input->offset]
        | (uint16_t)((uint16_t)input->bytes[input->offset + 1U] << 8U));
    input->offset += 2U;
    return 1;
}

static int take_u32(cursor *input, uint32_t *value)
{
    uint16_t low;
    uint16_t high;
    if (!take_u16(input, &low) || !take_u16(input, &high))
        return 0;
    *value = (uint32_t)low | ((uint32_t)high << 16U);
    return 1;
}

static vcc_dat_result decode_layer(cursor *record, uint8_t *output)
{
    uint16_t compressed_size;
    size_t compressed_end;
    size_t produced = 0U;

    if (!take_u16(record, &compressed_size))
        return VCC_DAT_TRUNCATED;
    if ((size_t)compressed_size > record->size - record->offset)
        return VCC_DAT_TRUNCATED;
    compressed_end = record->offset + (size_t)compressed_size;

    while (record->offset < compressed_end) {
        uint8_t value;
        uint8_t count = 1U;
        if (!take_u8(record, &value))
            return VCC_DAT_TRUNCATED;
        if (value == UINT8_C(0xFF)) {
            if (!take_u8(record, &count) || !take_u8(record, &value) || count == 0U)
                return VCC_DAT_BAD_MAP;
        }
        if ((size_t)count > VCC_MAP_CELLS - produced)
            return VCC_DAT_BAD_MAP;
        memset(output + produced, value, count);
        produced += count;
    }
    return produced == VCC_MAP_CELLS ? VCC_DAT_OK : VCC_DAT_BAD_MAP;
}

static vcc_dat_result copy_text(char *output, size_t capacity,
                                const uint8_t *input, size_t length,
                                int password)
{
    size_t text_length = length;
    size_t index;
    if (text_length > 0U && input[text_length - 1U] == 0U)
        --text_length;
    if (text_length >= capacity)
        return VCC_DAT_TEXT_TOO_LONG;
    for (index = 0U; index < text_length; ++index)
        output[index] = (char)(password ? input[index] ^ UINT8_C(0x99) : input[index]);
    output[text_length] = '\0';
    return VCC_DAT_OK;
}

static vcc_dat_result parse_metadata(cursor *record, vcc_level *level)
{
    uint16_t metadata_size;
    size_t metadata_end;

    if (!take_u16(record, &metadata_size))
        return VCC_DAT_TRUNCATED;
    if ((size_t)metadata_size > record->size - record->offset)
        return VCC_DAT_TRUNCATED;
    metadata_end = record->offset + (size_t)metadata_size;
    while (record->offset < metadata_end) {
        uint8_t type;
        uint8_t length;
        vcc_dat_result result = VCC_DAT_OK;
        if (!take_u8(record, &type) || !take_u8(record, &length))
            return VCC_DAT_TRUNCATED;
        if ((size_t)length > metadata_end - record->offset)
            return VCC_DAT_BAD_RECORD;
        if (type == 3U)
            result = copy_text(level->title, sizeof level->title,
                               record->bytes + record->offset, length, 0);
        else if (type == 6U)
            result = copy_text(level->password, sizeof level->password,
                               record->bytes + record->offset, length, 1);
        else if (type == 7U)
            result = copy_text(level->hint, sizeof level->hint,
                               record->bytes + record->offset, length, 0);
        if (result != VCC_DAT_OK)
            return result;
        record->offset += length;
    }
    return record->offset == metadata_end ? VCC_DAT_OK : VCC_DAT_BAD_RECORD;
}

vcc_dat_result vcc_dat_parse(vcc_dat *output, const uint8_t *bytes, size_t size)
{
    cursor input = {bytes, size, 0U};
    uint32_t magic;
    uint16_t count;
    uint16_t index;

    if (!output || (!bytes && size != 0U))
        return VCC_DAT_INVALID_ARGUMENT;
    memset(output, 0, sizeof *output);
    if (!take_u32(&input, &magic) || !take_u16(&input, &count))
        return VCC_DAT_TRUNCATED;
    if (magic != VCC_DAT_MAGIC)
        return VCC_DAT_BAD_MAGIC;
    if (count > VCC_MAX_LEVELS)
        return VCC_DAT_TOO_MANY_LEVELS;
    output->level_count = count;

    for (index = 0U; index < count; ++index) {
        uint16_t record_size;
        uint16_t map_version;
        cursor record;
        vcc_dat_result result;
        if (!take_u16(&input, &record_size))
            return VCC_DAT_TRUNCATED;
        if ((size_t)record_size > input.size - input.offset)
            return VCC_DAT_TRUNCATED;
        record.bytes = input.bytes + input.offset;
        record.size = record_size;
        record.offset = 0U;
        if (!take_u16(&record, &output->levels[index].number)
            || !take_u16(&record, &output->levels[index].time_limit)
            || !take_u16(&record, &output->levels[index].chips_required)
            || !take_u16(&record, &map_version))
            return VCC_DAT_TRUNCATED;
        if (map_version != 1U)
            return VCC_DAT_BAD_RECORD;
        result = decode_layer(&record, output->levels[index].lower);
        if (result != VCC_DAT_OK)
            return result;
        result = decode_layer(&record, output->levels[index].upper);
        if (result != VCC_DAT_OK)
            return result;
        result = parse_metadata(&record, &output->levels[index]);
        if (result != VCC_DAT_OK)
            return result;
        if (record.offset != record.size)
            return VCC_DAT_BAD_RECORD;
        input.offset += record_size;
    }
    return input.offset == input.size ? VCC_DAT_OK : VCC_DAT_BAD_RECORD;
}

const char *vcc_dat_result_string(vcc_dat_result result)
{
    static const char *const messages[] = {
        "ok", "invalid argument", "truncated input", "bad DAT magic",
        "too many levels", "bad level record", "bad map encoding",
        "text field too long"
    };
    size_t index = (size_t)result;
    return index < sizeof messages / sizeof messages[0] ? messages[index] : "unknown error";
}
