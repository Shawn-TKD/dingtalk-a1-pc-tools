#include "a1/recording_policy.h"

a1_recording_storage_action_t a1_recording_storage_action(
    uint64_t available_bytes)
{
    if (available_bytes == 0u) {
        return A1_RECORDING_STORAGE_REBOOT;
    }
    if (available_bytes <= A1_RECORDING_CLEANUP_WATERMARK_BYTES) {
        return A1_RECORDING_STORAGE_DELETE_OLDEST;
    }
    if (available_bytes <= A1_RECORDING_REQUIRED_FREE_BYTES) {
        return A1_RECORDING_STORAGE_REJECT;
    }
    return A1_RECORDING_STORAGE_ACCEPT;
}

size_t a1_recording_flush_chunk_size(size_t remaining_cache_bytes)
{
    return remaining_cache_bytes < A1_RECORDING_CACHE_WRITE_SIZE
        ? remaining_cache_bytes
        : A1_RECORDING_CACHE_WRITE_SIZE;
}

uint64_t a1_recording_container_data_size(uint64_t file_size)
{
    return file_size > A1_RECORDING_CONTAINER_HEADER_SIZE
        ? file_size - A1_RECORDING_CONTAINER_HEADER_SIZE
        : 0u;
}

int a1_recording_should_index_during_flush(uint64_t active_duration_ms)
{
    return active_duration_ms >= A1_RECORDING_EARLY_INDEX_DURATION_MS;
}

a1_recording_finalize_action_t a1_recording_rotation_action(
    uint64_t file_size,
    int already_indexed)
{
    if (file_size < A1_RECORDING_ROTATION_MIN_FILE_SIZE) {
        return A1_RECORDING_FINALIZE_DELETE;
    }
    return already_indexed != 0
        ? A1_RECORDING_FINALIZE_UPDATE_INDEX
        : A1_RECORDING_FINALIZE_ADD_INDEX;
}

a1_recording_finalize_action_t a1_recording_stop_action(
    uint64_t active_duration_ms,
    uint64_t file_size,
    int already_indexed)
{
    if (active_duration_ms < A1_RECORDING_FINAL_MIN_DURATION_MS ||
        file_size < A1_RECORDING_FINAL_MIN_FILE_SIZE) {
        return A1_RECORDING_FINALIZE_DELETE;
    }
    return already_indexed != 0
        ? A1_RECORDING_FINALIZE_UPDATE_INDEX
        : A1_RECORDING_FINALIZE_ADD_INDEX;
}
