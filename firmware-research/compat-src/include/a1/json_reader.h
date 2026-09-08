#ifndef A1_JSON_READER_H
#define A1_JSON_READER_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    A1_JSON_STRING,
    A1_JSON_NUMBER,
    A1_JSON_OBJECT,
    A1_JSON_ARRAY,
    A1_JSON_TRUE,
    A1_JSON_FALSE,
    A1_JSON_NULL
} a1_json_type_t;

typedef struct {
    const uint8_t *bytes;
    size_t length;
    a1_json_type_t type;
} a1_json_value_t;

/* This is a small read-only adapter for the bounded JSON shapes used by the
 * A1 wire protocol. It is not intended to be an application JSON library. */
int a1_json_object_get(
    const uint8_t *json,
    size_t json_length,
    const char *key,
    a1_json_value_t *value);

int a1_json_validate_object(
    const uint8_t *json,
    size_t json_length);

int a1_json_array_count(
    const a1_json_value_t *array,
    size_t *count);

int a1_json_array_get(
    const a1_json_value_t *array,
    size_t index,
    a1_json_value_t *value);

int a1_json_string_copy(
    const a1_json_value_t *value,
    char *output,
    size_t output_capacity);

int a1_json_int64(
    const a1_json_value_t *value,
    int64_t *output);

int a1_json_uint64(
    const a1_json_value_t *value,
    uint64_t *output);

#endif
