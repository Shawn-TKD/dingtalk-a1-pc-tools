#include "a1/inbound_transfer.h"

#include <string.h>

#include "a1/raw_transfer.h"

enum {
    A1_INBOUND_BLOCK_HEADER_SIZE = 16
};

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

void a1_inbound_transfer_init(a1_inbound_transfer_t *transfer)
{
    if (transfer != NULL) {
        memset(transfer, 0, sizeof(*transfer));
    }
}

int a1_inbound_transfer_begin(a1_inbound_transfer_t *transfer,
                              a1_inbound_type_t type,
                              uint32_t total_size,
                              uint32_t resume_offset,
                              uint32_t expected_verify_hash)
{
    if (transfer == NULL ||
        (type != A1_INBOUND_OTA && type != A1_INBOUND_USER_IMAGE) ||
        total_size == 0u || resume_offset > total_size) {
        return -1;
    }
    a1_inbound_transfer_init(transfer);
    transfer->active = resume_offset != total_size;
    transfer->complete = resume_offset == total_size;
    transfer->type = type;
    transfer->total_size = total_size;
    transfer->received_size = resume_offset;
    transfer->expected_verify_hash = expected_verify_hash;
    return transfer->complete ? 1 : 0;
}

int a1_inbound_block_decode(const uint8_t *body,
                            size_t body_length,
                            a1_inbound_block_view_t *block)
{
    uint32_t type;
    uint32_t length;
    uint32_t crc;
    if (body == NULL || block == NULL ||
        body_length < A1_INBOUND_BLOCK_HEADER_SIZE) {
        return -1;
    }
    type = read_be32(body);
    if (type > 1u) {
        return -2;
    }
    length = read_be32(body + 12u);
    if (length == 0u || (uint64_t)length !=
        (uint64_t)(body_length - A1_INBOUND_BLOCK_HEADER_SIZE)) {
        return -3;
    }
    crc = read_be32(body + 4u);
    if (a1_crc32_bzip2(body + A1_INBOUND_BLOCK_HEADER_SIZE, length) != crc) {
        return -4;
    }
    block->type = (a1_inbound_type_t)type;
    block->crc = crc;
    block->sequence = read_be32(body + 8u);
    block->data_length = length;
    block->data = body + A1_INBOUND_BLOCK_HEADER_SIZE;
    return 0;
}

int a1_inbound_transfer_commit_block(a1_inbound_transfer_t *transfer,
                                     const a1_inbound_block_view_t *block)
{
    if (transfer == NULL || block == NULL || !transfer->active ||
        transfer->complete || transfer->type != block->type ||
        block->data_length == 0u ||
        block->data_length > transfer->total_size - transfer->received_size) {
        return -1;
    }
    transfer->received_size += block->data_length;
    transfer->last_sequence = block->sequence;
    if (transfer->received_size == transfer->total_size) {
        transfer->active = false;
        transfer->complete = true;
        return 1;
    }
    return 0;
}

void a1_inbound_transfer_cancel(a1_inbound_transfer_t *transfer)
{
    a1_inbound_transfer_init(transfer);
}
