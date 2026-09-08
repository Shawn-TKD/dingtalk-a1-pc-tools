#include "a1/audio_index.h"

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
           bytes[3];
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

int a1_audio_index_header_decode(
    const uint8_t bytes[A1_AUDIO_INDEX_HEADER_SIZE],
    a1_audio_index_header_t *header)
{
    if (bytes == NULL || header == NULL) {
        return -1;
    }
    header->live_count = read_be32(bytes);
    memcpy(header->opaque_format, bytes + 4, sizeof(header->opaque_format));
    return 0;
}

int a1_audio_index_header_encode(
    const a1_audio_index_header_t *header,
    uint8_t bytes[A1_AUDIO_INDEX_HEADER_SIZE])
{
    if (header == NULL || bytes == NULL) {
        return -1;
    }
    write_be32(bytes, header->live_count);
    memcpy(bytes + 4, header->opaque_format, sizeof(header->opaque_format));
    return 0;
}

int a1_audio_index_record_decode(
    const uint8_t bytes[A1_AUDIO_INDEX_RECORD_SIZE],
    a1_audio_index_record_t *record)
{
    if (bytes == NULL || record == NULL) {
        return -1;
    }
    record->flags = bytes[0];
    record->fid = ((uint64_t)bytes[1] << 32) | read_be32(bytes + 2);
    record->duration_seconds = read_be16(bytes + 6);
    return 0;
}

int a1_audio_index_record_encode(
    const a1_audio_index_record_t *record,
    uint8_t bytes[A1_AUDIO_INDEX_RECORD_SIZE])
{
    if (record == NULL || bytes == NULL || record->fid > A1_AUDIO_INDEX_MAX_FID ||
        record->duration_seconds == 0u ||
        (record->flags & A1_AUDIO_INDEX_DELETED) != 0u) {
        return -1;
    }
    bytes[0] = record->flags;
    bytes[1] = (uint8_t)(record->fid >> 32);
    write_be32(bytes + 2, (uint32_t)record->fid);
    write_be16(bytes + 6, record->duration_seconds);
    return 0;
}

int a1_audio_index_record_is_deleted(
    const uint8_t bytes[A1_AUDIO_INDEX_RECORD_SIZE])
{
    return bytes != NULL && (bytes[0] & A1_AUDIO_INDEX_DELETED) != 0u;
}

void a1_audio_index_record_mark_deleted(
    uint8_t bytes[A1_AUDIO_INDEX_RECORD_SIZE])
{
    if (bytes != NULL) {
        bytes[0] |= A1_AUDIO_INDEX_DELETED;
    }
}

size_t a1_audio_index_segment_for_ordinal(size_t ordinal)
{
    return ordinal / A1_AUDIO_INDEX_RECORDS_PER_SEGMENT;
}

uint64_t a1_audio_index_segment_offset(size_t segment)
{
    return A1_AUDIO_INDEX_HEADER_SIZE +
           (uint64_t)segment * A1_AUDIO_INDEX_SEGMENT_SIZE;
}

int a1_audio_index_should_compact(uint32_t physical_count, uint32_t live_count)
{
    return physical_count > 10000u &&
           (uint64_t)physical_count > (uint64_t)live_count * 2u;
}

size_t a1_audio_index_collect_newest(
    const uint8_t *records,
    size_t record_count,
    uint64_t start_fid,
    uint64_t end_fid,
    uint64_t *output,
    size_t output_capacity)
{
    size_t written = 0u;

    if (records == NULL || output == NULL || output_capacity == 0u) {
        return 0u;
    }
    while (record_count != 0u && written < output_capacity) {
        a1_audio_index_record_t record;
        const uint8_t *packed;

        --record_count;
        packed = records + record_count * A1_AUDIO_INDEX_RECORD_SIZE;
        a1_audio_index_record_decode(packed, &record);
        if (!a1_audio_index_record_is_deleted(packed) &&
            (start_fid == 0u || record.fid >= start_fid) &&
            (end_fid == 0u || record.fid <= end_fid)) {
            output[written++] = record.fid;
        }
    }
    return written;
}
