#ifndef A1_FILE_SYNC_H
#define A1_FILE_SYNC_H

#include <stddef.h>
#include <stdint.h>

#define A1_FILE_INDEX_HEADER_SIZE 4u
#define A1_FILE_INDEX_ENTRY_SIZE 8u
#define A1_FILE_INDEX_TRAILER_SIZE 8u
#define A1_FILE_BLOCK_HEADER_SIZE 16u
#define A1_FILE_BLOCK_TRAILER_SIZE 8u
#define A1_FILE_SEND_BLOCK_DATA_LIMIT 48000u

typedef struct {
    uint16_t flag;
    uint32_t fid;
    uint16_t status_or_duration_raw;
} a1_file_index_entry_t;

typedef struct {
    int present;
    int truncated;
} a1_file_index_metadata_t;

typedef enum {
    A1_FILE_LIST_PERMITTED = 0,
    A1_FILE_LIST_PERMITTED_FORCE_SYNC = 1,
    A1_FILE_LIST_DENIED_INCOGNITO = 2,
    A1_FILE_LIST_DENIED_NEVER_CONFIGURED = 3,
    A1_FILE_LIST_DENIED_LEGACY_PEER = 4
} a1_file_list_policy_result_t;

typedef struct {
    uint16_t leading_reserved;
    uint32_t fid;
    uint16_t middle_reserved;
    uint32_t number;
    uint32_t data_length;
    const uint8_t *data;
    int trailer_present;
    uint32_t trailing_reserved;
    uint32_t crc;
} a1_file_block_view_t;

typedef enum {
    A1_FILE_SEND_IDLE = 0,
    A1_FILE_SEND_SENDING = 1,
    A1_FILE_SEND_COMPLETE = 3,
    A1_FILE_SEND_ERROR = 4,
    A1_FILE_SEND_CANCELLED = 5
} a1_file_send_state_t;

typedef enum {
    A1_FILE_SEND_NEXT_NONE = 0,
    A1_FILE_SEND_NEXT_BLOCK = 1,
    A1_FILE_SEND_NEXT_RESEND = 2,
    A1_FILE_SEND_NEXT_COMPLETE = 3,
    A1_FILE_SEND_NEXT_ABORT = 4
} a1_file_send_next_action_t;

typedef struct {
    uint32_t file_size;
    uint32_t next_offset;
    uint32_t next_sequence;
    uint32_t current_offset;
    uint32_t current_sequence;
    uint32_t current_length;
    uint16_t retry_count;
    int resending;
    a1_file_send_state_t state;
} a1_file_send_t;

typedef enum {
    A1_FILE_RECEIVE_WRITE = 1,
    A1_FILE_RECEIVE_DUPLICATE = 2,
    A1_FILE_RECEIVE_COMPLETE = 3,
    A1_FILE_RECEIVE_WRONG_FID = -1,
    A1_FILE_RECEIVE_OUT_OF_ORDER = -2,
    A1_FILE_RECEIVE_CHANGED_RETRY = -3,
    A1_FILE_RECEIVE_SIZE_MISMATCH = -4,
    A1_FILE_RECEIVE_INVALID_ARGUMENT = -5
} a1_file_receive_result_t;

typedef struct {
    uint32_t fid;
    uint64_t expected_size;
    uint64_t received_size;
    uint32_t next_number;
    uint32_t previous_number;
    uint32_t previous_length;
    uint32_t previous_crc;
    int expected_size_known;
    int has_previous;
    int complete;
} a1_file_receive_t;

int a1_file_index_decode(
    const uint8_t *payload,
    size_t payload_length,
    a1_file_index_entry_t *entries,
    size_t entry_capacity,
    size_t *entry_count);

int a1_file_index_decode_full(
    const uint8_t *payload,
    size_t payload_length,
    a1_file_index_entry_t *entries,
    size_t entry_capacity,
    size_t *entry_count,
    a1_file_index_metadata_t *metadata);

int a1_file_index_encode(
    uint8_t *output,
    size_t output_capacity,
    uint16_t response_code,
    const a1_file_index_entry_t *entries,
    size_t entry_count,
    int truncated,
    size_t *written);

/* Stock V1.6.88 permits legacy peers only when force-sync is explicitly one.
 * Incognito state is returned as a diagnostic reason, not as another allow path. */
a1_file_list_policy_result_t a1_file_list_policy(
    int peer_supports_incognito,
    int force_sync_read_ok,
    int force_sync,
    int incognito_read_ok,
    int incognito_mode,
    int incognito_flag_read_ok,
    uint16_t incognito_flag);

int a1_file_block_decode(
    const uint8_t *payload,
    size_t payload_length,
    a1_file_block_view_t *block);

int a1_file_block_encode(
    uint8_t *output,
    size_t output_capacity,
    uint16_t leading_reserved,
    uint32_t fid,
    uint16_t middle_reserved,
    uint32_t number,
    const uint8_t *data,
    size_t data_length,
    size_t *written);

void a1_file_receive_begin(
    a1_file_receive_t *receive,
    uint32_t fid,
    uint64_t expected_size,
    int expected_size_known);

a1_file_receive_result_t a1_file_receive_accept(
    a1_file_receive_t *receive,
    const a1_file_block_view_t *block);

int a1_file_send_begin(a1_file_send_t *transfer, uint32_t file_size, uint32_t offset);
size_t a1_file_send_next_read(const a1_file_send_t *transfer, uint32_t *offset);
int a1_file_send_mark_sent(a1_file_send_t *transfer, size_t data_length, uint32_t *sequence);
a1_file_send_next_action_t a1_file_send_on_response(
    a1_file_send_t *transfer,
    uint16_t response_code);
void a1_file_send_cancel(a1_file_send_t *transfer);

#endif
