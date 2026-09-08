#ifndef A1_INBOUND_TRANSFER_H
#define A1_INBOUND_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    A1_INBOUND_OTA = 0,
    A1_INBOUND_USER_IMAGE = 1
} a1_inbound_type_t;

typedef struct {
    a1_inbound_type_t type;
    uint32_t crc;
    uint32_t sequence;
    uint32_t data_length;
    const uint8_t *data;
} a1_inbound_block_view_t;

typedef struct {
    bool active;
    bool complete;
    bool verified;
    a1_inbound_type_t type;
    uint32_t total_size;
    uint32_t received_size;
    uint32_t expected_verify_hash;
    uint32_t last_sequence;
} a1_inbound_transfer_t;

void a1_inbound_transfer_init(a1_inbound_transfer_t *transfer);
int a1_inbound_transfer_begin(a1_inbound_transfer_t *transfer,
                              a1_inbound_type_t type,
                              uint32_t total_size,
                              uint32_t resume_offset,
                              uint32_t expected_verify_hash);
int a1_inbound_block_decode(const uint8_t *body,
                            size_t body_length,
                            a1_inbound_block_view_t *block);
/* Returns 1 when the block completed the declared file. The caller writes the
 * data through its storage adapter before committing it here. */
int a1_inbound_transfer_commit_block(a1_inbound_transfer_t *transfer,
                                     const a1_inbound_block_view_t *block);
void a1_inbound_transfer_cancel(a1_inbound_transfer_t *transfer);

#endif
