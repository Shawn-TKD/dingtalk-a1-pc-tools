#ifndef A1_LIVE_AUDIO_H
#define A1_LIVE_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define A1_LIVE_AUDIO_HEADER_SIZE 28u
#define A1_LIVE_OPUS_UNIT_SIZE 84u
#define A1_UT_RECORD_SIZE 20u
#define A1_UT_ENVELOPE_OVERHEAD 10u

typedef struct {
    uint32_t fid;
    uint32_t sequence;
    uint32_t audio_length;
    size_t opus_unit_count;
    const uint8_t *audio;
} a1_live_audio_view_t;

typedef struct {
    uint16_t sequence;
    uint32_t device_timestamp;
    uint8_t class_id;
    uint8_t code;
    uint32_t argument0;
    uint32_t argument1;
} a1_ut_record_t;

int a1_live_audio_decode(
    const uint8_t *payload,
    size_t payload_length,
    a1_live_audio_view_t *audio);

const uint8_t *a1_live_audio_opus_unit(
    const a1_live_audio_view_t *audio,
    size_t index);

int a1_ut_envelope_count(
    const uint8_t *payload,
    size_t payload_length,
    size_t *record_count);

int a1_ut_record_decode(
    const uint8_t *payload,
    size_t payload_length,
    size_t index,
    a1_ut_record_t *record);

int a1_ut_record_is_marker(const a1_ut_record_t *record);

#endif
