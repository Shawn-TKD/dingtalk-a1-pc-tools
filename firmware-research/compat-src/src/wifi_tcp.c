#include "a1/wifi_tcp.h"

#include <string.h>

#include "a1/raw_transfer.h"

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

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void discard_returned_frame(a1_wifi_tcp_decoder_t *decoder)
{
    if (decoder->returned_length == 0u) {
        return;
    }
    decoder->length -= decoder->returned_length;
    if (decoder->length != 0u) {
        memmove(decoder->buffer,
                decoder->buffer + decoder->returned_length,
                decoder->length);
    }
    decoder->returned_length = 0u;
}

void a1_wifi_tcp_decoder_init(a1_wifi_tcp_decoder_t *decoder)
{
    if (decoder != NULL) {
        memset(decoder, 0, sizeof(*decoder));
    }
}

void a1_wifi_tcp_decoder_reset(a1_wifi_tcp_decoder_t *decoder)
{
    a1_wifi_tcp_decoder_init(decoder);
}

a1_wifi_tcp_result_t a1_wifi_tcp_decoder_feed(
    a1_wifi_tcp_decoder_t *decoder,
    const uint8_t *bytes,
    size_t length)
{
    if (decoder == NULL || (bytes == NULL && length != 0u)) {
        return A1_WIFI_TCP_INVALID_ARGUMENT;
    }
    if (decoder->faulted) {
        return A1_WIFI_TCP_FAULTED;
    }
    if (length > A1_WIFI_TCP_BUFFER_SIZE - decoder->length) {
        return A1_WIFI_TCP_BUFFER_FULL;
    }
    if (length != 0u) {
        memcpy(decoder->buffer + decoder->length, bytes, length);
        decoder->length += length;
    }
    return A1_WIFI_TCP_OK;
}

a1_wifi_tcp_result_t a1_wifi_tcp_decoder_next(
    a1_wifi_tcp_decoder_t *decoder,
    a1_frame_view_t *frame)
{
    uint32_t body_length;
    size_t packet_length;

    if (decoder == NULL || frame == NULL) {
        return A1_WIFI_TCP_INVALID_ARGUMENT;
    }
    if (decoder->faulted) {
        return A1_WIFI_TCP_FAULTED;
    }
    discard_returned_frame(decoder);
    if (decoder->length < A1_FRAME_HEADER_SIZE) {
        return A1_WIFI_TCP_NEED_MORE;
    }
    if (decoder->buffer[0] != A1_FRAME_REQUEST &&
        decoder->buffer[0] != A1_FRAME_RESPONSE) {
        decoder->faulted = 1;
        return A1_WIFI_TCP_INVALID_KIND;
    }

    body_length = read_be32(decoder->buffer + 4u);
    if (body_length > A1_WIFI_TCP_MAX_BODY_SIZE) {
        decoder->faulted = 1;
        return A1_WIFI_TCP_PACKET_TOO_LARGE;
    }
    packet_length = A1_FRAME_HEADER_SIZE + (size_t)body_length;
    if (decoder->length < packet_length) {
        return A1_WIFI_TCP_NEED_MORE;
    }

    frame->kind = decoder->buffer[0];
    frame->command = read_be16(decoder->buffer + 1u);
    frame->message_id = decoder->buffer[3];
    frame->body_length = body_length;
    frame->body = decoder->buffer + A1_FRAME_HEADER_SIZE;
    decoder->returned_length = packet_length;
    return A1_WIFI_TCP_OK;
}

int a1_wifi_tcp_command_supported(uint16_t command)
{
    return command == A1_WIFI_TCP_FILE_HEADER_COMMAND ||
           command == A1_WIFI_TCP_FILE_BLOCK_COMMAND;
}

int a1_wifi_tcp_ota_block_decode(
    const uint8_t *body,
    size_t body_length,
    a1_wifi_tcp_ota_block_view_t *block)
{
    uint32_t data_length;
    uint32_t crc;

    if (body == NULL || block == NULL ||
        body_length < A1_WIFI_TCP_OTA_BLOCK_HEADER_SIZE) {
        return -1;
    }
    data_length = read_be32(body + 12u);
    if ((uint64_t)data_length !=
        (uint64_t)(body_length - A1_WIFI_TCP_OTA_BLOCK_HEADER_SIZE)) {
        return -2;
    }
    crc = read_be32(body + 4u);
    if (a1_crc32_bzip2(body + A1_WIFI_TCP_OTA_BLOCK_HEADER_SIZE,
                       data_length) != crc) {
        return -3;
    }
    if (read_be32(body) != 0u) {
        return -4;
    }

    block->transfer_type = 0u;
    block->crc = crc;
    block->sequence = read_be32(body + 8u);
    block->data_length = data_length;
    block->data = body + A1_WIFI_TCP_OTA_BLOCK_HEADER_SIZE;
    return 0;
}

int a1_wifi_tcp_ota_block_encode(
    uint8_t *output,
    size_t output_capacity,
    uint32_t sequence,
    const uint8_t *data,
    size_t data_length,
    size_t *written)
{
    size_t required_length;

    if (output == NULL || written == NULL ||
        (data == NULL && data_length != 0u) ||
        data_length > A1_WIFI_TCP_MAX_BODY_SIZE - A1_WIFI_TCP_OTA_BLOCK_HEADER_SIZE) {
        return -1;
    }
    required_length = A1_WIFI_TCP_OTA_BLOCK_HEADER_SIZE + data_length;
    if (output_capacity < required_length) {
        return -2;
    }

    write_be32(output, 0u);
    write_be32(output + 4u, a1_crc32_bzip2(data, data_length));
    write_be32(output + 8u, sequence);
    write_be32(output + 12u, (uint32_t)data_length);
    if (data_length != 0u) {
        memcpy(output + A1_WIFI_TCP_OTA_BLOCK_HEADER_SIZE, data, data_length);
    }
    *written = required_length;
    return 0;
}
