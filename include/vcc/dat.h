#ifndef VCC_DAT_H
#define VCC_DAT_H

#include <stddef.h>
#include <stdint.h>

#define VCC_DAT_MAGIC UINT32_C(0x0002AAAC)
#define VCC_MAP_WIDTH 32U
#define VCC_MAP_HEIGHT 32U
#define VCC_MAP_CELLS (VCC_MAP_WIDTH * VCC_MAP_HEIGHT)
#define VCC_MAX_LEVELS 149U
#define VCC_TITLE_CAPACITY 64U
#define VCC_HINT_CAPACITY 256U
#define VCC_PASSWORD_CAPACITY 5U

typedef struct vcc_level {
    uint16_t number;
    uint16_t time_limit;
    uint16_t chips_required;
    uint8_t lower[VCC_MAP_CELLS];
    uint8_t upper[VCC_MAP_CELLS];
    char title[VCC_TITLE_CAPACITY];
    char hint[VCC_HINT_CAPACITY];
    char password[VCC_PASSWORD_CAPACITY];
} vcc_level;

typedef struct vcc_dat {
    uint16_t level_count;
    vcc_level levels[VCC_MAX_LEVELS];
} vcc_dat;

typedef enum vcc_dat_result {
    VCC_DAT_OK = 0,
    VCC_DAT_INVALID_ARGUMENT,
    VCC_DAT_TRUNCATED,
    VCC_DAT_BAD_MAGIC,
    VCC_DAT_TOO_MANY_LEVELS,
    VCC_DAT_BAD_RECORD,
    VCC_DAT_BAD_MAP,
    VCC_DAT_TEXT_TOO_LONG
} vcc_dat_result;

vcc_dat_result vcc_dat_parse(vcc_dat *output, const uint8_t *bytes, size_t size);
const char *vcc_dat_result_string(vcc_dat_result result);

#endif

