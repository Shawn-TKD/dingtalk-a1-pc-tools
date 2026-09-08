#ifndef A1_FRAME_H
#define A1_FRAME_H

#include <stddef.h>
#include <stdint.h>

#define A1_FRAME_HEADER_SIZE 8u
#define A1_RX_BODY_LIMIT 39992u
#define A1_TX_BODY_LIMIT 60000u

typedef enum {
    A1_FRAME_REQUEST = 0x13,
    A1_FRAME_NOTIFY = 0x14,
    A1_FRAME_RESPONSE = 0x31
} a1_frame_kind_t;

typedef struct {
    uint8_t kind;
    uint16_t command;
    uint8_t message_id;
    uint32_t body_length;
    const uint8_t *body;
} a1_frame_view_t;

typedef enum {
    A1_FRAME_OK = 0,
    A1_FRAME_NEED_MORE = 1,
    A1_FRAME_INVALID_KIND = -1,
    A1_FRAME_BODY_TOO_LARGE = -2,
    A1_FRAME_INVALID_ARGUMENT = -3
} a1_frame_result_t;

a1_frame_result_t a1_frame_decode(
    const uint8_t *bytes,
    size_t length,
    a1_frame_view_t *frame,
    size_t *consumed);

a1_frame_result_t a1_frame_encode_header(
    uint8_t output[A1_FRAME_HEADER_SIZE],
    uint8_t kind,
    uint16_t command,
    uint8_t message_id,
    uint32_t body_length);

a1_frame_result_t a1_frame_encode(
    uint8_t *output,
    size_t output_capacity,
    uint8_t kind,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length,
    size_t *written);

#endif
