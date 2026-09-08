#ifndef A1_JSON_WRITER_H
#define A1_JSON_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define A1_JSON_WRITER_MAX_DEPTH 8u

typedef enum {
    A1_JSON_WRITER_OBJECT = 1,
    A1_JSON_WRITER_ARRAY = 2
} a1_json_writer_container_t;

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    size_t depth;
    unsigned count[A1_JSON_WRITER_MAX_DEPTH];
    uint8_t container[A1_JSON_WRITER_MAX_DEPTH];
    bool expecting_value[A1_JSON_WRITER_MAX_DEPTH];
    bool root_written;
    int error;
} a1_json_writer_t;

void a1_json_writer_init(
    a1_json_writer_t *writer,
    char *buffer,
    size_t capacity);
int a1_json_writer_begin_object(a1_json_writer_t *writer);
int a1_json_writer_end_object(a1_json_writer_t *writer);
int a1_json_writer_begin_array(a1_json_writer_t *writer);
int a1_json_writer_end_array(a1_json_writer_t *writer);
int a1_json_writer_key(a1_json_writer_t *writer, const char *key);
int a1_json_writer_string(a1_json_writer_t *writer, const char *value);
int a1_json_writer_int64(a1_json_writer_t *writer, int64_t value);
int a1_json_writer_uint64(a1_json_writer_t *writer, uint64_t value);
int a1_json_writer_bool(a1_json_writer_t *writer, bool value);
int a1_json_writer_null(a1_json_writer_t *writer);
int a1_json_writer_finish(a1_json_writer_t *writer, size_t *written);

#endif
