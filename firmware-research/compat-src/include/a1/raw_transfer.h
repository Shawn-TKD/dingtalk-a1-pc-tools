#ifndef A1_RAW_TRANSFER_H
#define A1_RAW_TRANSFER_H

#include <stddef.h>
#include <stdint.h>

#define A1_RAW_BLOCK_DATA_LIMIT 8000u
#define A1_RAW_BLOCK_OVERHEAD 12u

typedef struct {
    uint32_t sequence;
    uint32_t data_length;
    const uint8_t *data;
    uint32_t crc;
} a1_raw_block_view_t;

typedef enum {
    A1_RAW_TRANSFER_IDLE = 0,
    A1_RAW_TRANSFER_SENDING = 1,
    A1_RAW_TRANSFER_COMPLETE = 3,
    A1_RAW_TRANSFER_ERROR = 4,
    A1_RAW_TRANSFER_CANCELLED = 5
} a1_raw_transfer_state_t;

typedef enum {
    A1_RAW_NEXT_NONE = 0,
    A1_RAW_NEXT_SEND = 1,
    A1_RAW_NEXT_RESEND = 2,
    A1_RAW_NEXT_COMPLETE = 3,
    A1_RAW_NEXT_ABORT = 4
} a1_raw_next_action_t;

typedef struct {
    uint32_t file_size;
    uint32_t next_offset;
    uint32_t next_sequence;
    uint32_t current_offset;
    uint32_t current_sequence;
    uint32_t current_length;
    uint16_t retry_count;
    int resending;
    a1_raw_transfer_state_t state;
} a1_raw_transfer_t;

uint32_t a1_crc32_bzip2(const uint8_t *bytes, size_t length);

int a1_raw_block_decode(
    const uint8_t *body,
    size_t body_length,
    a1_raw_block_view_t *block);

int a1_raw_block_encode(
    uint8_t *output,
    size_t output_capacity,
    uint32_t sequence,
    const uint8_t *data,
    size_t data_length,
    size_t *written);

int a1_raw_transfer_begin(
    a1_raw_transfer_t *transfer,
    uint32_t file_size,
    uint32_t offset);

size_t a1_raw_transfer_next_read(
    const a1_raw_transfer_t *transfer,
    uint32_t *offset);

int a1_raw_transfer_mark_sent(
    a1_raw_transfer_t *transfer,
    size_t data_length,
    uint32_t *sequence);

a1_raw_next_action_t a1_raw_transfer_on_response(
    a1_raw_transfer_t *transfer,
    uint16_t response_code);

void a1_raw_transfer_cancel(a1_raw_transfer_t *transfer);

#endif
