#ifndef A1_AUDIO_CONTAINER_H
#define A1_AUDIO_CONTAINER_H

#include <stddef.h>
#include <stdint.h>

#define A1_DTYJ_SCAN_LIMIT 4096u
#define A1_OGG_PAGE_HEADER_SIZE 27u

typedef struct {
    uint32_t sample_rate;
    uint16_t record_size;
    uint32_t data_size;
    size_t record_count;
    const uint8_t *records;
} a1_dtyj_view_t;

int a1_dtyj_decode(
    const uint8_t *file_bytes,
    size_t file_length,
    a1_dtyj_view_t *recording);

const uint8_t *a1_dtyj_packet(
    const a1_dtyj_view_t *recording,
    size_t index,
    size_t *packet_length,
    uint32_t *opaque_prefix);

int a1_opus_packet_samples_48k(
    const uint8_t *packet,
    size_t packet_length,
    uint32_t *samples);

uint32_t a1_ogg_crc(const uint8_t *bytes, size_t length);

size_t a1_ogg_page_size(size_t packet_length);

int a1_ogg_page_encode(
    uint8_t *output,
    size_t output_capacity,
    const uint8_t *packet,
    size_t packet_length,
    uint8_t header_type,
    uint64_t granule_position,
    uint32_t serial,
    uint32_t sequence,
    size_t *written);

#endif
