#include "a1/json_writer.h"

#include <string.h>

enum {
    A1_JSON_WRITE_INVALID = -1,
    A1_JSON_WRITE_FULL = -2,
    A1_JSON_WRITE_DEPTH = -3,
    A1_JSON_WRITE_STATE = -4
};

static int fail(a1_json_writer_t *writer, int error)
{
    if (writer != NULL && writer->error == 0) {
        writer->error = error;
    }
    return error;
}

static int append_bytes(
    a1_json_writer_t *writer,
    const char *bytes,
    size_t length)
{
    if (writer == NULL || bytes == NULL || writer->buffer == NULL ||
        writer->capacity == 0u) {
        return fail(writer, A1_JSON_WRITE_INVALID);
    }
    if (writer->error != 0) {
        return writer->error;
    }
    if (length >= writer->capacity - writer->length) {
        return fail(writer, A1_JSON_WRITE_FULL);
    }
    memcpy(writer->buffer + writer->length, bytes, length);
    writer->length += length;
    writer->buffer[writer->length] = '\0';
    return 0;
}

static int append_byte(a1_json_writer_t *writer, char byte)
{
    return append_bytes(writer, &byte, 1u);
}

static int before_value(a1_json_writer_t *writer)
{
    size_t parent;

    if (writer == NULL || writer->error != 0) {
        return writer == NULL ? A1_JSON_WRITE_INVALID : writer->error;
    }
    if (writer->depth == 0u) {
        if (writer->root_written) {
            return fail(writer, A1_JSON_WRITE_STATE);
        }
        writer->root_written = true;
        return 0;
    }
    parent = writer->depth - 1u;
    if (writer->container[parent] == A1_JSON_WRITER_ARRAY) {
        if (writer->count[parent] != 0u && append_byte(writer, ',') != 0) {
            return writer->error;
        }
        writer->count[parent] += 1u;
        return 0;
    }
    if (writer->container[parent] != A1_JSON_WRITER_OBJECT ||
        !writer->expecting_value[parent]) {
        return fail(writer, A1_JSON_WRITE_STATE);
    }
    writer->expecting_value[parent] = false;
    writer->count[parent] += 1u;
    return 0;
}

static int write_quoted(a1_json_writer_t *writer, const char *value)
{
    static const char HEX[] = "0123456789abcdef";
    const uint8_t *bytes = (const uint8_t *)value;

    if (value == NULL || append_byte(writer, '"') != 0) {
        return value == NULL ? fail(writer, A1_JSON_WRITE_INVALID) : writer->error;
    }
    while (*bytes != 0u) {
        uint8_t byte = *bytes++;
        const char *escape = NULL;
        switch (byte) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default: break;
        }
        if (escape != NULL) {
            if (append_bytes(writer, escape, 2u) != 0) {
                return writer->error;
            }
        } else if (byte < 0x20u) {
            char encoded[6] = {'\\', 'u', '0', '0',
                               HEX[byte >> 4u], HEX[byte & 0x0fu]};
            if (append_bytes(writer, encoded, sizeof(encoded)) != 0) {
                return writer->error;
            }
        } else if (append_byte(writer, (char)byte) != 0) {
            return writer->error;
        }
    }
    return append_byte(writer, '"');
}

static int begin_container(a1_json_writer_t *writer, uint8_t type, char opening)
{
    size_t depth;

    if (writer == NULL) {
        return A1_JSON_WRITE_INVALID;
    }
    if (writer->depth >= A1_JSON_WRITER_MAX_DEPTH) {
        return fail(writer, A1_JSON_WRITE_DEPTH);
    }
    if (before_value(writer) != 0 || append_byte(writer, opening) != 0) {
        return writer->error;
    }
    depth = writer->depth++;
    writer->container[depth] = type;
    writer->count[depth] = 0u;
    writer->expecting_value[depth] = false;
    return 0;
}

static int end_container(a1_json_writer_t *writer, uint8_t type, char closing)
{
    size_t depth;

    if (writer == NULL || writer->error != 0 || writer->depth == 0u) {
        return writer == NULL ? A1_JSON_WRITE_INVALID
                              : fail(writer, A1_JSON_WRITE_STATE);
    }
    depth = writer->depth - 1u;
    if (writer->container[depth] != type || writer->expecting_value[depth]) {
        return fail(writer, A1_JSON_WRITE_STATE);
    }
    if (append_byte(writer, closing) != 0) {
        return writer->error;
    }
    writer->depth -= 1u;
    return 0;
}

static int write_unsigned_digits(a1_json_writer_t *writer, uint64_t value)
{
    char digits[20];
    size_t count = 0u;
    size_t index;

    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (index = 0u; index < count / 2u; ++index) {
        char temporary = digits[index];
        digits[index] = digits[count - index - 1u];
        digits[count - index - 1u] = temporary;
    }
    return append_bytes(writer, digits, count);
}

void a1_json_writer_init(
    a1_json_writer_t *writer,
    char *buffer,
    size_t capacity)
{
    if (writer == NULL) {
        return;
    }
    memset(writer, 0, sizeof(*writer));
    writer->buffer = buffer;
    writer->capacity = capacity;
    if (buffer == NULL || capacity == 0u) {
        writer->error = A1_JSON_WRITE_INVALID;
    } else {
        buffer[0] = '\0';
    }
}

int a1_json_writer_begin_object(a1_json_writer_t *writer)
{
    return begin_container(writer, A1_JSON_WRITER_OBJECT, '{');
}

int a1_json_writer_end_object(a1_json_writer_t *writer)
{
    return end_container(writer, A1_JSON_WRITER_OBJECT, '}');
}

int a1_json_writer_begin_array(a1_json_writer_t *writer)
{
    return begin_container(writer, A1_JSON_WRITER_ARRAY, '[');
}

int a1_json_writer_end_array(a1_json_writer_t *writer)
{
    return end_container(writer, A1_JSON_WRITER_ARRAY, ']');
}

int a1_json_writer_key(a1_json_writer_t *writer, const char *key)
{
    size_t depth;

    if (writer == NULL || writer->error != 0 || writer->depth == 0u) {
        return writer == NULL ? A1_JSON_WRITE_INVALID
                              : fail(writer, A1_JSON_WRITE_STATE);
    }
    depth = writer->depth - 1u;
    if (writer->container[depth] != A1_JSON_WRITER_OBJECT ||
        writer->expecting_value[depth]) {
        return fail(writer, A1_JSON_WRITE_STATE);
    }
    if (writer->count[depth] != 0u && append_byte(writer, ',') != 0) {
        return writer->error;
    }
    if (write_quoted(writer, key) != 0 || append_byte(writer, ':') != 0) {
        return writer->error;
    }
    writer->expecting_value[depth] = true;
    return 0;
}

int a1_json_writer_string(a1_json_writer_t *writer, const char *value)
{
    if (before_value(writer) != 0) {
        return writer == NULL ? A1_JSON_WRITE_INVALID : writer->error;
    }
    return write_quoted(writer, value);
}

int a1_json_writer_uint64(a1_json_writer_t *writer, uint64_t value)
{
    if (before_value(writer) != 0) {
        return writer == NULL ? A1_JSON_WRITE_INVALID : writer->error;
    }
    return write_unsigned_digits(writer, value);
}

int a1_json_writer_int64(a1_json_writer_t *writer, int64_t value)
{
    uint64_t magnitude;

    if (before_value(writer) != 0) {
        return writer == NULL ? A1_JSON_WRITE_INVALID : writer->error;
    }
    if (value < 0) {
        if (append_byte(writer, '-') != 0) {
            return writer->error;
        }
        magnitude = (uint64_t)(-(value + 1)) + 1u;
    } else {
        magnitude = (uint64_t)value;
    }
    return write_unsigned_digits(writer, magnitude);
}

int a1_json_writer_bool(a1_json_writer_t *writer, bool value)
{
    if (before_value(writer) != 0) {
        return writer == NULL ? A1_JSON_WRITE_INVALID : writer->error;
    }
    return append_bytes(writer, value ? "true" : "false", value ? 4u : 5u);
}

int a1_json_writer_null(a1_json_writer_t *writer)
{
    if (before_value(writer) != 0) {
        return writer == NULL ? A1_JSON_WRITE_INVALID : writer->error;
    }
    return append_bytes(writer, "null", 4u);
}

int a1_json_writer_finish(a1_json_writer_t *writer, size_t *written)
{
    if (writer == NULL || written == NULL) {
        return A1_JSON_WRITE_INVALID;
    }
    if (writer->error != 0) {
        return writer->error;
    }
    if (!writer->root_written || writer->depth != 0u) {
        return fail(writer, A1_JSON_WRITE_STATE);
    }
    *written = writer->length;
    return 0;
}
