#include "a1/raw_transfer.h"

#include <string.h>

#define A1_CRC32_POLYNOMIAL 0x04c11db7u

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

uint32_t a1_crc32_bzip2(const uint8_t *bytes, size_t length)
{
    uint32_t crc = 0xffffffffu;
    size_t index;
    int bit;

    if (bytes == NULL && length != 0) {
        return 0;
    }
    for (index = 0; index < length; ++index) {
        crc ^= (uint32_t)bytes[index] << 24;
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80000000u) != 0
                ? (crc << 1) ^ A1_CRC32_POLYNOMIAL
                : crc << 1;
        }
    }
    return crc ^ 0xffffffffu;
}

int a1_raw_block_decode(
    const uint8_t *body,
    size_t body_length,
    a1_raw_block_view_t *block)
{
    uint32_t data_length;
    uint32_t expected_crc;

    if (body == NULL || block == NULL || body_length < A1_RAW_BLOCK_OVERHEAD) {
        return -1;
    }
    data_length = read_be32(body + 4);
    if (data_length > A1_RAW_BLOCK_DATA_LIMIT ||
        body_length != A1_RAW_BLOCK_OVERHEAD + (size_t)data_length) {
        return -2;
    }
    expected_crc = read_be32(body + 8 + data_length);
    if (a1_crc32_bzip2(body + 8, data_length) != expected_crc) {
        return -3;
    }

    block->sequence = read_be32(body);
    block->data_length = data_length;
    block->data = body + 8;
    block->crc = expected_crc;
    return 0;
}

int a1_raw_block_encode(
    uint8_t *output,
    size_t output_capacity,
    uint32_t sequence,
    const uint8_t *data,
    size_t data_length,
    size_t *written)
{
    size_t total_length;

    if (output == NULL || written == NULL ||
        (data == NULL && data_length != 0) ||
        data_length > A1_RAW_BLOCK_DATA_LIMIT) {
        return -1;
    }
    total_length = A1_RAW_BLOCK_OVERHEAD + data_length;
    if (output_capacity < total_length) {
        return -2;
    }

    write_be32(output, sequence);
    write_be32(output + 4, (uint32_t)data_length);
    if (data_length != 0) {
        memcpy(output + 8, data, data_length);
    }
    write_be32(output + 8 + data_length, a1_crc32_bzip2(data, data_length));
    *written = total_length;
    return 0;
}

int a1_raw_transfer_begin(
    a1_raw_transfer_t *transfer,
    uint32_t file_size,
    uint32_t offset)
{
    if (transfer == NULL || offset >= file_size) {
        return -1;
    }

    memset(transfer, 0, sizeof(*transfer));
    transfer->file_size = file_size;
    transfer->next_offset = offset;
    transfer->state = A1_RAW_TRANSFER_SENDING;
    return 0;
}

size_t a1_raw_transfer_next_read(
    const a1_raw_transfer_t *transfer,
    uint32_t *offset)
{
    uint32_t remaining;

    if (transfer == NULL || offset == NULL ||
        transfer->state != A1_RAW_TRANSFER_SENDING ||
        transfer->next_offset >= transfer->file_size) {
        return 0;
    }
    *offset = transfer->next_offset;
    remaining = transfer->file_size - transfer->next_offset;
    return remaining < A1_RAW_BLOCK_DATA_LIMIT
        ? (size_t)remaining
        : A1_RAW_BLOCK_DATA_LIMIT;
}

int a1_raw_transfer_mark_sent(
    a1_raw_transfer_t *transfer,
    size_t data_length,
    uint32_t *sequence)
{
    uint32_t offset;
    size_t expected_length;

    if (transfer == NULL || sequence == NULL) {
        return -1;
    }
    expected_length = a1_raw_transfer_next_read(transfer, &offset);
    if (expected_length == 0 || data_length != expected_length) {
        return -2;
    }

    transfer->next_sequence += 1;
    transfer->current_sequence = transfer->next_sequence;
    transfer->current_offset = offset;
    transfer->current_length = (uint32_t)data_length;
    transfer->next_offset += (uint32_t)data_length;
    if (!transfer->resending) {
        transfer->retry_count = 0;
    }
    transfer->resending = 0;
    *sequence = transfer->current_sequence;
    return 0;
}

a1_raw_next_action_t a1_raw_transfer_on_response(
    a1_raw_transfer_t *transfer,
    uint16_t response_code)
{
    if (transfer == NULL || transfer->state != A1_RAW_TRANSFER_SENDING ||
        transfer->current_length == 0) {
        return A1_RAW_NEXT_NONE;
    }

    if (response_code == 200) {
        transfer->retry_count = 0;
        if (transfer->next_offset < transfer->file_size) {
            return A1_RAW_NEXT_SEND;
        }
        transfer->state = A1_RAW_TRANSFER_COMPLETE;
        return A1_RAW_NEXT_COMPLETE;
    }

    if (response_code == 0x0193 && transfer->retry_count < 3) {
        transfer->retry_count += 1;
        transfer->next_offset = transfer->current_offset;
        transfer->next_sequence = transfer->current_sequence - 1;
        transfer->resending = 1;
        return A1_RAW_NEXT_RESEND;
    }

    transfer->state = A1_RAW_TRANSFER_ERROR;
    return A1_RAW_NEXT_ABORT;
}

void a1_raw_transfer_cancel(a1_raw_transfer_t *transfer)
{
    if (transfer != NULL && transfer->state == A1_RAW_TRANSFER_SENDING) {
        transfer->state = A1_RAW_TRANSFER_CANCELLED;
    }
}
