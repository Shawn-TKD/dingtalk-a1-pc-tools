#include "a1/json_reader.h"

#include <limits.h>
#include <string.h>

#define A1_JSON_MAX_DEPTH 16u

static size_t skip_space(const uint8_t *json, size_t length, size_t offset)
{
    while (offset < length &&
           (json[offset] == ' ' || json[offset] == '\t' ||
            json[offset] == '\r' || json[offset] == '\n')) {
        offset += 1u;
    }
    return offset;
}

static int hex_value(uint8_t value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static int scan_string(
    const uint8_t *json,
    size_t length,
    size_t offset,
    size_t *end)
{
    if (offset >= length || json[offset] != '"') {
        return -1;
    }
    offset += 1u;
    while (offset < length) {
        uint8_t value = json[offset++];
        if (value == '"') {
            *end = offset;
            return 0;
        }
        if (value < 0x20u) {
            return -1;
        }
        if (value == '\\') {
            size_t digits;
            if (offset >= length) {
                return -1;
            }
            value = json[offset++];
            if (value == 'u') {
                if (length - offset < 4u) {
                    return -1;
                }
                for (digits = 0u; digits < 4u; ++digits) {
                    if (hex_value(json[offset + digits]) < 0) {
                        return -1;
                    }
                }
                offset += 4u;
            } else if (strchr("\"\\/bfnrt", value) == NULL) {
                return -1;
            }
        }
    }
    return -1;
}

static int scan_number(
    const uint8_t *json,
    size_t length,
    size_t offset,
    size_t *end)
{
    if (offset < length && json[offset] == '-') {
        offset += 1u;
    }
    if (offset >= length) {
        return -1;
    }
    if (json[offset] == '0') {
        offset += 1u;
        if (offset < length && json[offset] >= '0' && json[offset] <= '9') {
            return -1;
        }
    } else if (json[offset] >= '1' && json[offset] <= '9') {
        do {
            offset += 1u;
        } while (offset < length && json[offset] >= '0' && json[offset] <= '9');
    } else {
        return -1;
    }
    if (offset < length && json[offset] == '.') {
        offset += 1u;
        if (offset >= length || json[offset] < '0' || json[offset] > '9') {
            return -1;
        }
        do {
            offset += 1u;
        } while (offset < length && json[offset] >= '0' && json[offset] <= '9');
    }
    if (offset < length && (json[offset] == 'e' || json[offset] == 'E')) {
        offset += 1u;
        if (offset < length && (json[offset] == '+' || json[offset] == '-')) {
            offset += 1u;
        }
        if (offset >= length || json[offset] < '0' || json[offset] > '9') {
            return -1;
        }
        do {
            offset += 1u;
        } while (offset < length && json[offset] >= '0' && json[offset] <= '9');
    }
    *end = offset;
    return 0;
}

static int skip_value(
    const uint8_t *json,
    size_t length,
    size_t offset,
    unsigned depth,
    size_t *end,
    a1_json_type_t *type)
{
    uint8_t opening;
    uint8_t closing;
    size_t item_end;

    if (depth > A1_JSON_MAX_DEPTH) {
        return -1;
    }
    offset = skip_space(json, length, offset);
    if (offset >= length) {
        return -1;
    }
    if (json[offset] == '"') {
        *type = A1_JSON_STRING;
        return scan_string(json, length, offset, end);
    }
    if (json[offset] == '{' || json[offset] == '[') {
        opening = json[offset];
        closing = opening == '{' ? '}' : ']';
        *type = opening == '{' ? A1_JSON_OBJECT : A1_JSON_ARRAY;
        offset = skip_space(json, length, offset + 1u);
        if (offset < length && json[offset] == closing) {
            *end = offset + 1u;
            return 0;
        }
        while (offset < length) {
            if (opening == '{') {
                if (scan_string(json, length, offset, &item_end) != 0) {
                    return -1;
                }
                offset = skip_space(json, length, item_end);
                if (offset >= length || json[offset] != ':') {
                    return -1;
                }
                offset += 1u;
            }
            if (skip_value(json, length, offset, depth + 1u,
                           &item_end, type) != 0) {
                return -1;
            }
            offset = skip_space(json, length, item_end);
            if (offset < length && json[offset] == closing) {
                *end = offset + 1u;
                *type = opening == '{' ? A1_JSON_OBJECT : A1_JSON_ARRAY;
                return 0;
            }
            if (offset >= length || json[offset] != ',') {
                return -1;
            }
            offset = skip_space(json, length, offset + 1u);
        }
        return -1;
    }
    if (length - offset >= 4u && memcmp(json + offset, "true", 4u) == 0) {
        *type = A1_JSON_TRUE;
        *end = offset + 4u;
        return 0;
    }
    if (length - offset >= 5u && memcmp(json + offset, "false", 5u) == 0) {
        *type = A1_JSON_FALSE;
        *end = offset + 5u;
        return 0;
    }
    if (length - offset >= 4u && memcmp(json + offset, "null", 4u) == 0) {
        *type = A1_JSON_NULL;
        *end = offset + 4u;
        return 0;
    }
    if (json[offset] == '-' || (json[offset] >= '0' && json[offset] <= '9')) {
        *type = A1_JSON_NUMBER;
        return scan_number(json, length, offset, end);
    }
    return -1;
}

static int key_matches(
    const uint8_t *json,
    size_t start,
    size_t end,
    const char *key)
{
    size_t key_length = strlen(key);
    if (end < start + 2u || end - start - 2u != key_length) {
        return 0;
    }
    return memcmp(json + start + 1u, key, key_length) == 0;
}

int a1_json_object_get(
    const uint8_t *json,
    size_t json_length,
    const char *key,
    a1_json_value_t *value)
{
    size_t offset;
    size_t key_end;
    size_t value_start;
    size_t value_end;
    size_t root_end;
    a1_json_type_t type;
    a1_json_type_t root_type;

    if (json == NULL || key == NULL || value == NULL) {
        return -1;
    }
    offset = skip_space(json, json_length, 0u);
    if (skip_value(json, json_length, offset, 0u, &root_end, &root_type) != 0 ||
        root_type != A1_JSON_OBJECT ||
        skip_space(json, json_length, root_end) != json_length) {
        return -1;
    }
    offset = skip_space(json, json_length, offset + 1u);
    while (offset < json_length && json[offset] != '}') {
        size_t key_start = offset;
        if (scan_string(json, json_length, key_start, &key_end) != 0) {
            return -1;
        }
        offset = skip_space(json, json_length, key_end);
        if (offset >= json_length || json[offset] != ':') {
            return -1;
        }
        value_start = skip_space(json, json_length, offset + 1u);
        if (skip_value(json, json_length, value_start, 1u,
                       &value_end, &type) != 0) {
            return -1;
        }
        if (key_matches(json, key_start, key_end, key)) {
            value->type = type;
            if (type == A1_JSON_STRING) {
                value->bytes = json + value_start + 1u;
                value->length = value_end - value_start - 2u;
            } else {
                value->bytes = json + value_start;
                value->length = value_end - value_start;
            }
            return 0;
        }
        offset = skip_space(json, json_length, value_end);
        if (offset < json_length && json[offset] == ',') {
            offset = skip_space(json, json_length, offset + 1u);
        } else if (offset >= json_length || json[offset] != '}') {
            return -1;
        }
    }
    return 1;
}

int a1_json_validate_object(
    const uint8_t *json,
    size_t json_length)
{
    size_t offset;
    size_t end;
    a1_json_type_t type;

    if (json == NULL) {
        return -1;
    }
    offset = skip_space(json, json_length, 0u);
    if (skip_value(json, json_length, offset, 0u, &end, &type) != 0 ||
        type != A1_JSON_OBJECT ||
        skip_space(json, json_length, end) != json_length) {
        return -1;
    }
    return 0;
}

int a1_json_array_count(
    const a1_json_value_t *array,
    size_t *count)
{
    size_t offset;
    size_t end;
    a1_json_type_t type;
    size_t array_end;

    if (array == NULL || count == NULL || array->type != A1_JSON_ARRAY ||
        array->bytes == NULL || array->length < 2u) {
        return -1;
    }
    if (skip_value(array->bytes, array->length, 0u, 0u,
                   &array_end, &type) != 0 ||
        type != A1_JSON_ARRAY ||
        skip_space(array->bytes, array->length, array_end) != array->length) {
        return -1;
    }
    *count = 0u;
    offset = skip_space(array->bytes, array->length, 1u);
    while (offset < array->length && array->bytes[offset] != ']') {
        if (skip_value(array->bytes, array->length, offset, 1u, &end, &type) != 0) {
            return -1;
        }
        *count += 1u;
        offset = skip_space(array->bytes, array->length, end);
        if (offset < array->length && array->bytes[offset] == ',') {
            offset = skip_space(array->bytes, array->length, offset + 1u);
        } else if (offset >= array->length || array->bytes[offset] != ']') {
            return -1;
        }
    }
    return offset < array->length && array->bytes[offset] == ']' ? 0 : -1;
}

int a1_json_array_get(
    const a1_json_value_t *array,
    size_t index,
    a1_json_value_t *value)
{
    size_t offset;
    size_t end;
    size_t current = 0u;
    a1_json_type_t type;
    a1_json_type_t array_type;
    size_t array_end;

    if (array == NULL || value == NULL || array->type != A1_JSON_ARRAY ||
        array->bytes == NULL || array->length < 2u) {
        return -1;
    }
    if (skip_value(array->bytes, array->length, 0u, 0u,
                   &array_end, &array_type) != 0 ||
        array_type != A1_JSON_ARRAY ||
        skip_space(array->bytes, array->length, array_end) != array->length) {
        return -1;
    }
    offset = skip_space(array->bytes, array->length, 1u);
    while (offset < array->length && array->bytes[offset] != ']') {
        size_t start = offset;
        if (skip_value(array->bytes, array->length, start, 1u, &end, &type) != 0) {
            return -1;
        }
        if (current == index) {
            value->bytes = array->bytes + start;
            value->length = end - start;
            value->type = type;
            return 0;
        }
        current += 1u;
        offset = skip_space(array->bytes, array->length, end);
        if (offset < array->length && array->bytes[offset] == ',') {
            offset = skip_space(array->bytes, array->length, offset + 1u);
        } else if (offset >= array->length || array->bytes[offset] != ']') {
            return -1;
        }
    }
    return 1;
}

int a1_json_string_copy(
    const a1_json_value_t *value,
    char *output,
    size_t output_capacity)
{
    size_t input = 0u;
    size_t written = 0u;

    if (value == NULL || output == NULL || output_capacity == 0u ||
        value->type != A1_JSON_STRING || value->bytes == NULL) {
        return -1;
    }
    while (input < value->length) {
        uint8_t byte = value->bytes[input++];
        if (byte == '\\') {
            if (input >= value->length) {
                return -1;
            }
            byte = value->bytes[input++];
            switch (byte) {
            case '"': case '\\': case '/': break;
            case 'b': byte = '\b'; break;
            case 'f': byte = '\f'; break;
            case 'n': byte = '\n'; break;
            case 'r': byte = '\r'; break;
            case 't': byte = '\t'; break;
            case 'u': {
                uint32_t codepoint = 0u;
                size_t digit;
                int hex;
                if (value->length - input < 4u) {
                    return -1;
                }
                for (digit = 0u; digit < 4u; ++digit) {
                    hex = hex_value(value->bytes[input + digit]);
                    if (hex < 0) {
                        return -1;
                    }
                    codepoint = codepoint * 16u + (uint32_t)hex;
                }
                input += 4u;
                if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
                    uint32_t low = 0u;
                    if (value->length - input < 6u ||
                        value->bytes[input] != '\\' ||
                        value->bytes[input + 1u] != 'u') {
                        return -2;
                    }
                    input += 2u;
                    for (digit = 0u; digit < 4u; ++digit) {
                        hex = hex_value(value->bytes[input + digit]);
                        if (hex < 0) {
                            return -1;
                        }
                        low = low * 16u + (uint32_t)hex;
                    }
                    input += 4u;
                    if (low < 0xdc00u || low > 0xdfffu) {
                        return -2;
                    }
                    codepoint = 0x10000u +
                        ((codepoint - 0xd800u) << 10u) + (low - 0xdc00u);
                } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
                    return -2;
                }
                if (codepoint <= 0x7fu) {
                    if (written + 1u >= output_capacity) {
                        return -3;
                    }
                    output[written++] = (char)codepoint;
                } else if (codepoint <= 0x7ffu) {
                    if (written + 2u >= output_capacity) {
                        return -3;
                    }
                    output[written++] = (char)(0xc0u | (codepoint >> 6u));
                    output[written++] = (char)(0x80u | (codepoint & 0x3fu));
                } else if (codepoint <= 0xffffu) {
                    if (written + 3u >= output_capacity) {
                        return -3;
                    }
                    output[written++] = (char)(0xe0u | (codepoint >> 12u));
                    output[written++] = (char)(0x80u | ((codepoint >> 6u) & 0x3fu));
                    output[written++] = (char)(0x80u | (codepoint & 0x3fu));
                } else {
                    if (written + 4u >= output_capacity) {
                        return -3;
                    }
                    output[written++] = (char)(0xf0u | (codepoint >> 18u));
                    output[written++] = (char)(0x80u | ((codepoint >> 12u) & 0x3fu));
                    output[written++] = (char)(0x80u | ((codepoint >> 6u) & 0x3fu));
                    output[written++] = (char)(0x80u | (codepoint & 0x3fu));
                }
                continue;
            }
            default: return -2;
            }
        }
        if (written + 1u >= output_capacity) {
            return -3;
        }
        output[written++] = (char)byte;
    }
    output[written] = '\0';
    return 0;
}

int a1_json_int64(
    const a1_json_value_t *value,
    int64_t *output)
{
    const uint8_t *bytes;
    size_t length;
    size_t index = 0u;
    uint64_t magnitude = 0u;
    uint64_t limit;
    int negative = 0;

    if (value == NULL || output == NULL || value->bytes == NULL ||
        (value->type != A1_JSON_NUMBER && value->type != A1_JSON_STRING)) {
        return -1;
    }
    bytes = value->bytes;
    length = value->length;
    if (length != 0u && bytes[0] == '-') {
        negative = 1;
        index = 1u;
    }
    if (index == length) {
        return -1;
    }
    limit = negative ? (uint64_t)INT64_MAX + 1u : (uint64_t)INT64_MAX;
    for (; index < length; ++index) {
        uint8_t digit;
        if (bytes[index] < '0' || bytes[index] > '9') {
            return -1;
        }
        digit = (uint8_t)(bytes[index] - '0');
        if (magnitude > (limit - digit) / 10u) {
            return -2;
        }
        magnitude = magnitude * 10u + digit;
    }
    if (negative) {
        *output = magnitude == (uint64_t)INT64_MAX + 1u
            ? INT64_MIN
            : -(int64_t)magnitude;
    } else {
        *output = (int64_t)magnitude;
    }
    return 0;
}

int a1_json_uint64(
    const a1_json_value_t *value,
    uint64_t *output)
{
    const uint8_t *bytes;
    size_t length;
    size_t index;
    uint64_t magnitude = 0u;

    if (value == NULL || output == NULL || value->bytes == NULL ||
        (value->type != A1_JSON_NUMBER && value->type != A1_JSON_STRING)) {
        return -1;
    }
    bytes = value->bytes;
    length = value->length;
    if (length == 0u || bytes[0] == '-') {
        return -1;
    }
    for (index = 0u; index < length; ++index) {
        uint8_t digit;
        if (bytes[index] < '0' || bytes[index] > '9') {
            return -1;
        }
        digit = (uint8_t)(bytes[index] - '0');
        if (magnitude > (UINT64_MAX - digit) / 10u) {
            return -2;
        }
        magnitude = magnitude * 10u + digit;
    }
    *output = magnitude;
    return 0;
}
