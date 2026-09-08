#ifndef A1_AUDIO_INDEX_H
#define A1_AUDIO_INDEX_H

#include <stddef.h>
#include <stdint.h>

#define A1_AUDIO_INDEX_HEADER_SIZE 8u
#define A1_AUDIO_INDEX_RECORD_SIZE 8u
#define A1_AUDIO_INDEX_RECORDS_PER_SEGMENT 1000u
#define A1_AUDIO_INDEX_SEGMENT_SIZE 8000u
#define A1_AUDIO_INDEX_MAX_SEGMENTS 100u
#define A1_AUDIO_INDEX_MAX_FID UINT64_C(0xffffffffff)
#define A1_AUDIO_INDEX_DELETED 0x80u

typedef struct {
    uint32_t live_count;
    uint8_t opaque_format[4];
} a1_audio_index_header_t;

typedef struct {
    uint64_t fid;
    uint16_t duration_seconds;
    uint8_t flags;
} a1_audio_index_record_t;

int a1_audio_index_header_decode(
    const uint8_t bytes[A1_AUDIO_INDEX_HEADER_SIZE],
    a1_audio_index_header_t *header);

int a1_audio_index_header_encode(
    const a1_audio_index_header_t *header,
    uint8_t bytes[A1_AUDIO_INDEX_HEADER_SIZE]);

int a1_audio_index_record_decode(
    const uint8_t bytes[A1_AUDIO_INDEX_RECORD_SIZE],
    a1_audio_index_record_t *record);

/* New stock records use flags zero. The compatible codec permits the known
 * lower seven flag bits but rejects the tombstone bit on creation. */
int a1_audio_index_record_encode(
    const a1_audio_index_record_t *record,
    uint8_t bytes[A1_AUDIO_INDEX_RECORD_SIZE]);

int a1_audio_index_record_is_deleted(
    const uint8_t bytes[A1_AUDIO_INDEX_RECORD_SIZE]);

void a1_audio_index_record_mark_deleted(
    uint8_t bytes[A1_AUDIO_INDEX_RECORD_SIZE]);

size_t a1_audio_index_segment_for_ordinal(size_t ordinal);
uint64_t a1_audio_index_segment_offset(size_t segment);

int a1_audio_index_should_compact(uint32_t physical_count, uint32_t live_count);

/* Collect live FIDs newest-first from an ascending packed record array. A zero
 * bound means open-ended on that side, matching the product's list behavior. */
size_t a1_audio_index_collect_newest(
    const uint8_t *records,
    size_t record_count,
    uint64_t start_fid,
    uint64_t end_fid,
    uint64_t *output,
    size_t output_capacity);

#endif
