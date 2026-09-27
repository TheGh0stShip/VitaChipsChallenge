#include "vcc/dat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    FILE *file = fopen(VCC_REFERENCE_DAT, "rb");
    uint8_t *bytes;
    long length;
    vcc_dat parsed;
    if (!file || fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0
        || fseek(file, 0, SEEK_SET) != 0)
        return 1;
    bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1U, (size_t)length, file) != (size_t)length)
        return 1;
    fclose(file);
    {
        vcc_dat_result result = vcc_dat_parse(&parsed, bytes, (size_t)length);
        if (result != VCC_DAT_OK) {
            fprintf(stderr, "reference DAT parse failed: %s\n",
                    vcc_dat_result_string(result));
            return 1;
        }
    }
    free(bytes);
    if (parsed.level_count != 149U
        || strcmp(parsed.levels[0].title, "LESSON 1") != 0
        || strcmp(parsed.levels[0].password, "BDHP") != 0
        || parsed.levels[0].time_limit != 100U
        || parsed.levels[0].chips_required != 11U
        || parsed.levels[4].trap_count != 2U
        || parsed.levels[4].clone_count != 1U
        || parsed.levels[4].creature_count != 3U)
        return 1;
    puts("reference DAT verified: 149 levels, first level LESSON 1 / BDHP");
    return 0;
}
