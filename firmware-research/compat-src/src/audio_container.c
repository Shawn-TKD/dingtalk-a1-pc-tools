#include "a1/audio_container.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t *bytes, uint64_t value)
{
    size_t index;
    for (index = 0; index < 8u; ++index) {
        bytes[index] = (uint8_t)value;
        value >>= 8;
    }
}

static size_t find_tag(
    const uint8_t *bytes,
    size_t length,
    size_t start,
    const char tag[4])
{
    size_t index;
    if (start > length || length - start < 4u) {
        return SIZE_MAX;
    }
    for (index = start; index + 4u <= length; ++index) {
        if (memcmp(bytes + index, tag, 4u) == 0) {
            return index;
        }
    }
    return SIZE_MAX;
}

int a1_dtyj_decode(
    const uint8_t *file_bytes,
    size_t file_length,
    a1_dtyj_view_t *recording)
{
    size_t scan_length;
    size_t fmt_offset;
    size_t data_offset;
    size_t records_offset;
    uint32_t declared_length;
    uint32_t data_size;
    uint16_t record_size;

    if (file_bytes == NULL || recording == NULL || file_length < 12u) {
        return -1;
    }
    if (memcmp(file_bytes, "BABA", 4u) != 0 ||
        memcmp(file_bytes + 8u, "DTYJ", 4u) != 0) {
        return -2;
    }
    declared_length = read_le32(file_bytes + 4u);
    if ((uint64_t)declared_length + 8u != (uint64_t)file_length) {
        return -3;
    }

    scan_length = file_length < A1_DTYJ_SCAN_LIMIT ? file_length : A1_DTYJ_SCAN_LIMIT;
    fmt_offset = find_tag(file_bytes, scan_length, 12u, "fmt ");
    if (fmt_offset == SIZE_MAX || fmt_offset + 26u > scan_length) {
        return -4;
    }
    data_offset = find_tag(file_bytes, scan_length, fmt_offset + 4u, "data");
    if (data_offset == SIZE_MAX || data_offset + 12u > file_length) {
        return -4;
    }

    record_size = read_le16(file_bytes + fmt_offset + 24u);
    data_size = read_le32(file_bytes + data_offset + 4u);
    records_offset = data_offset + 12u;
    if (record_size <= 4u || data_size % record_size != 0u ||
        (uint64_t)records_offset + data_size > (uint64_t)file_length) {
        return -5;
    }

    recording->sample_rate = read_le32(file_bytes + fmt_offset + 12u);
    recording->record_size = record_size;
    recording->data_size = data_size;
    recording->record_count = data_size / record_size;
    recording->records = file_bytes + records_offset;
    return 0;
}

const uint8_t *a1_dtyj_packet(
    const a1_dtyj_view_t *recording,
    size_t index,
    size_t *packet_length,
    uint32_t *opaque_prefix)
{
    const uint8_t *record;

    if (recording == NULL || recording->records == NULL ||
        packet_length == NULL || index >= recording->record_count ||
        recording->record_size <= 4u) {
        return NULL;
    }
    record = recording->records + index * recording->record_size;
    *packet_length = recording->record_size - 4u;
    if (opaque_prefix != NULL) {
        *opaque_prefix = read_le32(record);
    }
    return record + 4u;
}

int a1_opus_packet_samples_48k(
    const uint8_t *packet,
    size_t packet_length,
    uint32_t *samples)
{
    uint8_t config;
    uint8_t frame_code;
    uint32_t samples_per_frame;
    uint32_t frame_count;

    if (packet == NULL || samples == NULL || packet_length == 0u) {
        return -1;
    }
    config = packet[0] >> 3;
    frame_code = packet[0] & 3u;
    if (config < 12u) {
        static const uint16_t values[] = {480u, 960u, 1920u, 2880u};
        samples_per_frame = values[config % 4u];
    } else if (config < 16u) {
        static const uint16_t values[] = {480u, 960u};
        samples_per_frame = values[config % 2u];
    } else {
        static const uint16_t values[] = {120u, 240u, 480u, 960u};
        samples_per_frame = values[config % 4u];
    }

    if (frame_code == 0u) {
        frame_count = 1u;
    } else if (frame_code == 3u) {
        if (packet_length < 2u) {
            return -2;
        }
        frame_count = packet[1] & 63u;
    } else {
        frame_count = 2u;
    }
    if (frame_count == 0u || frame_count * samples_per_frame > 5760u) {
        return -3;
    }
    *samples = frame_count * samples_per_frame;
    return 0;
}

uint32_t a1_ogg_crc(const uint8_t *bytes, size_t length)
{
    uint32_t crc = 0u;
    size_t index;
    int bit;

    if (bytes == NULL && length != 0u) {
        return 0u;
    }
    for (index = 0; index < length; ++index) {
        crc ^= (uint32_t)bytes[index] << 24;
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80000000u) != 0u
                ? (crc << 1) ^ 0x04c11db7u
                : crc << 1;
        }
    }
    return crc;
}

size_t a1_ogg_page_size(size_t packet_length)
{
    size_t lacing_count = packet_length / 255u + 1u;
    if (lacing_count > 255u || packet_length > SIZE_MAX - A1_OGG_PAGE_HEADER_SIZE - lacing_count) {
        return 0u;
    }
    return A1_OGG_PAGE_HEADER_SIZE + lacing_count + packet_length;
}

int a1_ogg_page_encode(
    uint8_t *output,
    size_t output_capacity,
    const uint8_t *packet,
    size_t packet_length,
    uint8_t header_type,
    uint64_t granule_position,
    uint32_t serial,
    uint32_t sequence,
    size_t *written)
{
    size_t lacing_count;
    size_t output_length;
    size_t index;

    if (output == NULL || written == NULL || (packet == NULL && packet_length != 0u)) {
        return -1;
    }
    output_length = a1_ogg_page_size(packet_length);
    if (output_length == 0u || output_capacity < output_length) {
        return -2;
    }
    lacing_count = packet_length / 255u + 1u;
    memset(output, 0, output_length);
    memcpy(output, "OggS", 4u);
    output[4] = 0u;
    output[5] = header_type;
    write_le64(output + 6u, granule_position);
    write_le32(output + 14u, serial);
    write_le32(output + 18u, sequence);
    output[26] = (uint8_t)lacing_count;
    for (index = 0; index + 1u < lacing_count; ++index) {
        output[A1_OGG_PAGE_HEADER_SIZE + index] = 255u;
    }
    output[A1_OGG_PAGE_HEADER_SIZE + lacing_count - 1u] =
        (uint8_t)(packet_length % 255u);
    if (packet_length != 0u) {
        memcpy(output + A1_OGG_PAGE_HEADER_SIZE + lacing_count, packet, packet_length);
    }
    write_le32(output + 22u, a1_ogg_crc(output, output_length));
    *written = output_length;
    return 0;
}
