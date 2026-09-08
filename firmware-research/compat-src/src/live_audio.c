#include "a1/live_audio.h"

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

int a1_live_audio_decode(
    const uint8_t *payload,
    size_t payload_length,
    a1_live_audio_view_t *audio)
{
    uint32_t audio_length;

    if (payload == NULL || audio == NULL || payload_length < A1_LIVE_AUDIO_HEADER_SIZE) {
        return -1;
    }
    audio_length = read_be32(payload + 20);
    if ((uint64_t)audio_length > (uint64_t)(payload_length - A1_LIVE_AUDIO_HEADER_SIZE)) {
        return -2;
    }
    if (audio_length % A1_LIVE_OPUS_UNIT_SIZE != 0u) {
        return -3;
    }

    audio->fid = read_be32(payload + 4);
    audio->sequence = read_be32(payload + 16);
    audio->audio_length = audio_length;
    audio->opus_unit_count = audio_length / A1_LIVE_OPUS_UNIT_SIZE;
    audio->audio = payload + A1_LIVE_AUDIO_HEADER_SIZE;
    return 0;
}

const uint8_t *a1_live_audio_opus_unit(
    const a1_live_audio_view_t *audio,
    size_t index)
{
    if (audio == NULL || audio->audio == NULL || index >= audio->opus_unit_count) {
        return NULL;
    }
    return audio->audio + index * A1_LIVE_OPUS_UNIT_SIZE;
}

int a1_ut_envelope_count(
    const uint8_t *payload,
    size_t payload_length,
    size_t *record_count)
{
    uint16_t records_length;
    uint32_t declared_count;
    size_t calculated_count;

    if (payload == NULL || record_count == NULL || payload_length < A1_UT_ENVELOPE_OVERHEAD ||
        payload[0] != 'Z' || payload[1] != 'Z' ||
        payload[payload_length - 2u] != 'Z' || payload[payload_length - 1u] != 'Z') {
        return -1;
    }
    records_length = read_be16(payload + 2);
    if (records_length % A1_UT_RECORD_SIZE != 0u ||
        payload_length != (size_t)records_length + A1_UT_ENVELOPE_OVERHEAD) {
        return -2;
    }
    calculated_count = records_length / A1_UT_RECORD_SIZE;
    declared_count = read_be32(payload + payload_length - 6u);
    if ((uint64_t)declared_count != (uint64_t)calculated_count) {
        return -3;
    }
    *record_count = calculated_count;
    return 0;
}

int a1_ut_record_decode(
    const uint8_t *payload,
    size_t payload_length,
    size_t index,
    a1_ut_record_t *record)
{
    size_t count;
    size_t offset;
    int result;

    if (record == NULL) {
        return -1;
    }
    result = a1_ut_envelope_count(payload, payload_length, &count);
    if (result != 0) {
        return result;
    }
    if (index >= count) {
        return -4;
    }
    offset = 4u + index * A1_UT_RECORD_SIZE;
    record->sequence = read_be16(payload + offset);
    record->device_timestamp = read_be32(payload + offset + 4u);
    record->class_id = payload[offset + 8u];
    record->code = payload[offset + 11u];
    record->argument0 = read_be32(payload + offset + 12u);
    record->argument1 = read_be32(payload + offset + 16u);
    return 0;
}

int a1_ut_record_is_marker(const a1_ut_record_t *record)
{
    return record != NULL &&
           record->class_id == 0x0bu &&
           record->code == 1u &&
           record->argument0 == 0u;
}
