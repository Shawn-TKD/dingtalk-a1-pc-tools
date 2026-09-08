#ifndef A1_RECORDING_POLICY_H
#define A1_RECORDING_POLICY_H

#include <stddef.h>
#include <stdint.h>

#define A1_RECORDING_CONTAINER_HEADER_SIZE 80u
#define A1_RECORDING_CACHE_WRITE_SIZE 1024u
#define A1_RECORDING_ROTATION_MIN_FILE_SIZE 2048u
#define A1_RECORDING_FINAL_MIN_FILE_SIZE 4800u
#define A1_RECORDING_FINAL_MIN_DURATION_MS 1200u
#define A1_RECORDING_EARLY_INDEX_DURATION_MS 5000u
#define A1_RECORDING_CLEANUP_WATERMARK_BYTES UINT64_C(0x20000000)
#define A1_RECORDING_REQUIRED_FREE_BYTES UINT64_C(0x30000000)

typedef enum {
    A1_RECORDING_STORAGE_ACCEPT = 0,
    A1_RECORDING_STORAGE_DELETE_OLDEST = 1,
    A1_RECORDING_STORAGE_REJECT = 2,
    A1_RECORDING_STORAGE_REBOOT = 3
} a1_recording_storage_action_t;

typedef enum {
    A1_RECORDING_FINALIZE_DELETE = 0,
    A1_RECORDING_FINALIZE_ADD_INDEX = 1,
    A1_RECORDING_FINALIZE_UPDATE_INDEX = 2
} a1_recording_finalize_action_t;

a1_recording_storage_action_t a1_recording_storage_action(
    uint64_t available_bytes);

size_t a1_recording_flush_chunk_size(size_t remaining_cache_bytes);
uint64_t a1_recording_container_data_size(uint64_t file_size);

int a1_recording_should_index_during_flush(uint64_t active_duration_ms);

a1_recording_finalize_action_t a1_recording_rotation_action(
    uint64_t file_size,
    int already_indexed);

a1_recording_finalize_action_t a1_recording_stop_action(
    uint64_t active_duration_ms,
    uint64_t file_size,
    int already_indexed);

#endif
