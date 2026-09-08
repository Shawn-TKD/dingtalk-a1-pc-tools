#include "a1/firmware_update.h"

#include <limits.h>
#include <stddef.h>

static int parse_u32(const char **cursor, char delimiter, uint32_t *value)
{
    const char *text = *cursor;
    uint32_t result = 0u;
    if (*text < '0' || *text > '9') {
        return -1;
    }
    do {
        uint32_t digit = (uint32_t)(*text - '0');
        if (result > (UINT32_MAX - digit) / 10u) {
            return -1;
        }
        result = result * 10u + digit;
        text++;
    } while (*text >= '0' && *text <= '9');
    if (*text != delimiter) {
        return -1;
    }
    *value = result;
    *cursor = text + 1;
    return 0;
}

static int parse_u64_end(const char *text, uint64_t *value)
{
    uint64_t result = 0u;
    if (*text < '0' || *text > '9') {
        return -1;
    }
    do {
        uint64_t digit = (uint64_t)(*text - '0');
        if (result > (UINT64_MAX - digit) / UINT64_C(10)) {
            return -1;
        }
        result = result * UINT64_C(10) + digit;
        text++;
    } while (*text >= '0' && *text <= '9');
    if (*text != '\0') {
        return -1;
    }
    *value = result;
    return 0;
}

int a1_firmware_version_parse(const char *text, a1_firmware_version_t *version)
{
    const char *cursor;
    a1_firmware_version_t parsed;
    if (text == NULL || version == NULL || text[0] != 'V') {
        return -1;
    }
    cursor = text + 1;
    if (parse_u32(&cursor, '.', &parsed.major) != 0 ||
        parse_u32(&cursor, '.', &parsed.minor) != 0 ||
        parse_u32(&cursor, '-', &parsed.patch) != 0 ||
        parse_u64_end(cursor, &parsed.build) != 0) {
        return -2;
    }
    *version = parsed;
    return 0;
}

bool a1_firmware_version_is_newer(const char *candidate, const char *current)
{
    a1_firmware_version_t left;
    a1_firmware_version_t right;
    if (a1_firmware_version_parse(candidate, &left) != 0 ||
        a1_firmware_version_parse(current, &right) != 0) {
        return false;
    }
    if (left.major != right.major) return left.major > right.major;
    if (left.minor != right.minor) return left.minor > right.minor;
    if (left.patch != right.patch) return left.patch > right.patch;
    return left.build > right.build;
}
