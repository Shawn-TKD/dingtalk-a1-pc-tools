#include "a1/file_sync.h"

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

int a1_file_index_decode(
    const uint8_t *payload,
    size_t payload_length,
    a1_file_index_entry_t *entries,
    size_t entry_capacity,
    size_t *entry_count)
{
    return a1_file_index_decode_full(
        payload,
        payload_length,
        entries,
        entry_capacity,
        entry_count,
        NULL);
}

int a1_file_index_decode_full(
    const uint8_t *payload,
    size_t payload_length,
    a1_file_index_entry_t *entries,
    size_t entry_capacity,
    size_t *entry_count,
    a1_file_index_metadata_t *metadata)
{
    uint16_t response_code;
    uint16_t declared_count;
    size_t required_length;
    size_t trailing_length;
    size_t index;
    size_t offset;

    if (payload == NULL || entry_count == NULL || payload_length < A1_FILE_INDEX_HEADER_SIZE) {
        return -1;
    }
    response_code = read_be16(payload);
    if (response_code != 200u) {
        return -(int)response_code;
    }
    declared_count = read_be16(payload + 2);
    required_length = A1_FILE_INDEX_HEADER_SIZE +
        (size_t)declared_count * A1_FILE_INDEX_ENTRY_SIZE;
    if (payload_length < required_length) {
        return -2;
    }

    if (metadata != NULL) {
        memset(metadata, 0, sizeof(*metadata));
    }
    trailing_length = payload_length - required_length;
    if (trailing_length == A1_FILE_INDEX_TRAILER_SIZE &&
        payload[required_length] == 0u &&
        payload[required_length + 1u] == 0u &&
        payload[required_length + 2u] == 0u &&
        payload[required_length + 3u] <= 1u &&
        memcmp(payload + required_length + 4u, "ZZZZ", 4u) == 0) {
        if (metadata != NULL) {
            metadata->present = 1;
            metadata->truncated = payload[required_length + 3u] != 0u;
        }
    } else {
        /* Some owner captures and older peers omit the stock eight-byte trailer
         * or expose only transport padding. Keep that form interoperable. */
        for (index = required_length; index < payload_length; ++index) {
            if (payload[index] != 0u && payload[index] != 0x5au) {
                return -3;
            }
        }
    }
    *entry_count = declared_count;
    if (declared_count == 0u) {
        return 0;
    }
    if (entries == NULL || entry_capacity < declared_count) {
        return -4;
    }

    for (index = 0; index < declared_count; ++index) {
        offset = A1_FILE_INDEX_HEADER_SIZE + index * A1_FILE_INDEX_ENTRY_SIZE;
        entries[index].flag = read_be16(payload + offset);
        entries[index].fid = read_be32(payload + offset + 2);
        entries[index].status_or_duration_raw = read_be16(payload + offset + 6);
    }
    return 0;
}

int a1_file_index_encode(
    uint8_t *output,
    size_t output_capacity,
    uint16_t response_code,
    const a1_file_index_entry_t *entries,
    size_t entry_count,
    int truncated,
    size_t *written)
{
    size_t required_length;
    size_t index;
    size_t offset;

    if (output == NULL || written == NULL || entry_count > 300u ||
        (entries == NULL && entry_count != 0u)) {
        return -1;
    }
    required_length = A1_FILE_INDEX_HEADER_SIZE +
        entry_count * A1_FILE_INDEX_ENTRY_SIZE + A1_FILE_INDEX_TRAILER_SIZE;
    if (output_capacity < required_length) {
        return -2;
    }

    write_be16(output, response_code);
    write_be16(output + 2u, (uint16_t)entry_count);
    for (index = 0; index < entry_count; ++index) {
        offset = A1_FILE_INDEX_HEADER_SIZE + index * A1_FILE_INDEX_ENTRY_SIZE;
        write_be16(output + offset, entries[index].flag);
        write_be32(output + offset + 2u, entries[index].fid);
        write_be16(output + offset + 6u, entries[index].status_or_duration_raw);
    }
    offset = A1_FILE_INDEX_HEADER_SIZE + entry_count * A1_FILE_INDEX_ENTRY_SIZE;
    memset(output + offset, 0, 4u);
    output[offset + 3u] = truncated != 0;
    memcpy(output + offset + 4u, "ZZZZ", 4u);
    *written = required_length;
    return 0;
}

a1_file_list_policy_result_t a1_file_list_policy(
    int peer_supports_incognito,
    int force_sync_read_ok,
    int force_sync,
    int incognito_read_ok,
    int incognito_mode,
    int incognito_flag_read_ok,
    uint16_t incognito_flag)
{
    if (peer_supports_incognito != 0) {
        return A1_FILE_LIST_PERMITTED;
    }
    if (force_sync_read_ok != 0 && force_sync == 1) {
        return A1_FILE_LIST_PERMITTED_FORCE_SYNC;
    }
    if (incognito_read_ok != 0 && incognito_mode == 1) {
        return A1_FILE_LIST_DENIED_INCOGNITO;
    }
    if (incognito_flag_read_ok != 0 && incognito_flag != 0xaa55u) {
        return A1_FILE_LIST_DENIED_NEVER_CONFIGURED;
    }
    return A1_FILE_LIST_DENIED_LEGACY_PEER;
}

int a1_file_block_decode(
    const uint8_t *payload,
    size_t payload_length,
    a1_file_block_view_t *block)
{
    uint32_t data_length;

    if (payload == NULL || block == NULL || payload_length < A1_FILE_BLOCK_HEADER_SIZE) {
        return -1;
    }
    data_length = read_be32(payload + 12);
    if ((uint64_t)data_length > (uint64_t)(payload_length - A1_FILE_BLOCK_HEADER_SIZE)) {
        return -2;
    }

    block->leading_reserved = read_be16(payload);
    block->fid = read_be32(payload + 2);
    block->middle_reserved = read_be16(payload + 6);
    block->number = read_be32(payload + 8);
    block->data_length = data_length;
    block->data = payload + A1_FILE_BLOCK_HEADER_SIZE;
    block->trailer_present = 0;
    block->trailing_reserved = 0;
    block->crc = 0;
    if (payload_length == A1_FILE_BLOCK_HEADER_SIZE + (size_t)data_length) {
        return 0;
    }
    if (payload_length != A1_FILE_BLOCK_HEADER_SIZE + (size_t)data_length +
            A1_FILE_BLOCK_TRAILER_SIZE) {
        return -3;
    }
    block->trailer_present = 1;
    block->trailing_reserved = read_be32(block->data + data_length);
    block->crc = read_be32(block->data + data_length + 4u);
    if (a1_crc32_bzip2(block->data, data_length) != block->crc) {
        return -4;
    }
    return 0;
}

int a1_file_block_encode(
    uint8_t *output,
    size_t output_capacity,
    uint16_t leading_reserved,
    uint32_t fid,
    uint16_t middle_reserved,
    uint32_t number,
    const uint8_t *data,
    size_t data_length,
    size_t *written)
{
    size_t required_length;

    if (output == NULL || written == NULL ||
        (data == NULL && data_length != 0u) ||
        data_length > A1_FILE_SEND_BLOCK_DATA_LIMIT) {
        return -1;
    }
    required_length = A1_FILE_BLOCK_HEADER_SIZE + data_length +
        A1_FILE_BLOCK_TRAILER_SIZE;
    if (output_capacity < required_length) {
        return -2;
    }

    write_be16(output, leading_reserved);
    write_be32(output + 2u, fid);
    write_be16(output + 6u, middle_reserved);
    write_be32(output + 8u, number);
    write_be32(output + 12u, (uint32_t)data_length);
    if (data_length != 0u) {
        memcpy(output + A1_FILE_BLOCK_HEADER_SIZE, data, data_length);
    }
    write_be32(output + A1_FILE_BLOCK_HEADER_SIZE + data_length, 0u);
    write_be32(
        output + A1_FILE_BLOCK_HEADER_SIZE + data_length + 4u,
        a1_crc32_bzip2(data, data_length));
    *written = required_length;
    return 0;
}

void a1_file_receive_begin(
    a1_file_receive_t *receive,
    uint32_t fid,
    uint64_t expected_size,
    int expected_size_known)
{
    if (receive == NULL) {
        return;
    }
    memset(receive, 0, sizeof(*receive));
    receive->fid = fid;
    receive->expected_size = expected_size;
    receive->expected_size_known = expected_size_known != 0;
}

a1_file_receive_result_t a1_file_receive_accept(
    a1_file_receive_t *receive,
    const a1_file_block_view_t *block)
{
    uint32_t block_crc;

    if (receive == NULL || block == NULL ||
        (block->data == NULL && block->data_length != 0u) ||
        block->data_length == 0u || receive->complete) {
        return A1_FILE_RECEIVE_INVALID_ARGUMENT;
    }
    if (block->fid != receive->fid) {
        return A1_FILE_RECEIVE_WRONG_FID;
    }

    block_crc = a1_crc32_bzip2(block->data, block->data_length);
    if (receive->has_previous && block->number == receive->previous_number) {
        if (block->data_length != receive->previous_length || block_crc != receive->previous_crc) {
            return A1_FILE_RECEIVE_CHANGED_RETRY;
        }
        return A1_FILE_RECEIVE_DUPLICATE;
    }
    if (block->number != receive->next_number) {
        return A1_FILE_RECEIVE_OUT_OF_ORDER;
    }
    if (receive->expected_size_known &&
        receive->received_size + block->data_length > receive->expected_size) {
        return A1_FILE_RECEIVE_SIZE_MISMATCH;
    }

    receive->received_size += block->data_length;
    receive->previous_number = block->number;
    receive->previous_length = block->data_length;
    receive->previous_crc = block_crc;
    receive->has_previous = 1;
    receive->next_number = block->number + 1u;

    if (receive->expected_size_known && receive->received_size == receive->expected_size) {
        receive->complete = 1;
        return A1_FILE_RECEIVE_COMPLETE;
    }
    return A1_FILE_RECEIVE_WRITE;
}

int a1_file_send_begin(a1_file_send_t *transfer, uint32_t file_size, uint32_t offset)
{
    if (transfer == NULL || offset >= file_size) {
        return -1;
    }
    memset(transfer, 0, sizeof(*transfer));
    transfer->file_size = file_size;
    transfer->next_offset = offset;
    transfer->state = A1_FILE_SEND_SENDING;
    return 0;
}

size_t a1_file_send_next_read(const a1_file_send_t *transfer, uint32_t *offset)
{
    uint32_t remaining;

    if (transfer == NULL || offset == NULL ||
        transfer->state != A1_FILE_SEND_SENDING ||
        transfer->next_offset >= transfer->file_size) {
        return 0;
    }
    *offset = transfer->next_offset;
    remaining = transfer->file_size - transfer->next_offset;
    return remaining < A1_FILE_SEND_BLOCK_DATA_LIMIT
        ? (size_t)remaining
        : A1_FILE_SEND_BLOCK_DATA_LIMIT;
}

int a1_file_send_mark_sent(
    a1_file_send_t *transfer,
    size_t data_length,
    uint32_t *sequence)
{
    uint32_t offset;
    size_t expected_length;

    if (transfer == NULL || sequence == NULL) {
        return -1;
    }
    expected_length = a1_file_send_next_read(transfer, &offset);
    if (expected_length == 0u || data_length != expected_length) {
        return -2;
    }
    transfer->next_sequence += 1u;
    transfer->current_sequence = transfer->next_sequence;
    transfer->current_offset = offset;
    transfer->current_length = (uint32_t)data_length;
    transfer->next_offset += (uint32_t)data_length;
    if (!transfer->resending) {
        transfer->retry_count = 0u;
    }
    transfer->resending = 0;
    *sequence = transfer->current_sequence;
    return 0;
}

a1_file_send_next_action_t a1_file_send_on_response(
    a1_file_send_t *transfer,
    uint16_t response_code)
{
    if (transfer == NULL || transfer->state != A1_FILE_SEND_SENDING ||
        transfer->current_length == 0u) {
        return A1_FILE_SEND_NEXT_NONE;
    }
    if (response_code == 200u) {
        transfer->retry_count = 0u;
        if (transfer->next_offset < transfer->file_size) {
            return A1_FILE_SEND_NEXT_BLOCK;
        }
        transfer->state = A1_FILE_SEND_COMPLETE;
        return A1_FILE_SEND_NEXT_COMPLETE;
    }
    if (transfer->retry_count < 3u) {
        transfer->retry_count += 1u;
        transfer->next_offset = transfer->current_offset;
        transfer->next_sequence = transfer->current_sequence - 1u;
        transfer->resending = 1;
        return A1_FILE_SEND_NEXT_RESEND;
    }
    transfer->state = A1_FILE_SEND_ERROR;
    return A1_FILE_SEND_NEXT_ABORT;
}

void a1_file_send_cancel(a1_file_send_t *transfer)
{
    if (transfer != NULL && transfer->state == A1_FILE_SEND_SENDING) {
        transfer->state = A1_FILE_SEND_CANCELLED;
    }
}
