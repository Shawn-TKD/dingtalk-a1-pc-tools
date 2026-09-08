#include "a1/frame.h"

#include <string.h>

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

a1_frame_result_t a1_frame_decode(
    const uint8_t *bytes,
    size_t length,
    a1_frame_view_t *frame,
    size_t *consumed)
{
    uint32_t body_length;

    if (bytes == NULL || frame == NULL || consumed == NULL) {
        return A1_FRAME_INVALID_ARGUMENT;
    }
    *consumed = 0;
    if (length < A1_FRAME_HEADER_SIZE) {
        return A1_FRAME_NEED_MORE;
    }
    if (bytes[0] != A1_FRAME_REQUEST &&
        bytes[0] != A1_FRAME_NOTIFY &&
        bytes[0] != A1_FRAME_RESPONSE) {
        return A1_FRAME_INVALID_KIND;
    }

    body_length = read_be32(bytes + 4);
    if (body_length >
        (bytes[0] == A1_FRAME_REQUEST ? A1_RX_BODY_LIMIT : A1_TX_BODY_LIMIT)) {
        return A1_FRAME_BODY_TOO_LARGE;
    }
    if (length < A1_FRAME_HEADER_SIZE + (size_t)body_length) {
        return A1_FRAME_NEED_MORE;
    }

    frame->kind = bytes[0];
    frame->command = read_be16(bytes + 1);
    frame->message_id = bytes[3];
    frame->body_length = body_length;
    frame->body = bytes + A1_FRAME_HEADER_SIZE;
    *consumed = A1_FRAME_HEADER_SIZE + (size_t)body_length;
    return A1_FRAME_OK;
}

a1_frame_result_t a1_frame_encode_header(
    uint8_t output[A1_FRAME_HEADER_SIZE],
    uint8_t kind,
    uint16_t command,
    uint8_t message_id,
    uint32_t body_length)
{
    uint32_t body_limit;

    if (output == NULL) {
        return A1_FRAME_INVALID_ARGUMENT;
    }
    if (kind != A1_FRAME_REQUEST &&
        kind != A1_FRAME_NOTIFY &&
        kind != A1_FRAME_RESPONSE) {
        return A1_FRAME_INVALID_KIND;
    }
    body_limit = kind == A1_FRAME_REQUEST ? A1_RX_BODY_LIMIT : A1_TX_BODY_LIMIT;
    if (body_length > body_limit) {
        return A1_FRAME_BODY_TOO_LARGE;
    }

    output[0] = kind;
    write_be16(output + 1, command);
    output[3] = message_id;
    write_be32(output + 4, body_length);
    return A1_FRAME_OK;
}

a1_frame_result_t a1_frame_encode(
    uint8_t *output,
    size_t output_capacity,
    uint8_t kind,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length,
    size_t *written)
{
    a1_frame_result_t result;
    size_t total_length;

    if (output == NULL || written == NULL ||
        (body == NULL && body_length != 0u) || body_length > UINT32_MAX) {
        return A1_FRAME_INVALID_ARGUMENT;
    }
    total_length = A1_FRAME_HEADER_SIZE + body_length;
    if (total_length < body_length || output_capacity < total_length) {
        return A1_FRAME_NEED_MORE;
    }
    result = a1_frame_encode_header(output, kind, command, message_id,
                                    (uint32_t)body_length);
    if (result != A1_FRAME_OK) {
        return result;
    }
    if (body_length != 0u) {
        memcpy(output + A1_FRAME_HEADER_SIZE, body, body_length);
    }
    *written = total_length;
    return A1_FRAME_OK;
}
