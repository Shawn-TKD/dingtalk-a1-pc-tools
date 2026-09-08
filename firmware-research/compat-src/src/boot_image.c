#include "a1/boot_image.h"

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

int a1_mcu_image_header_decode(const uint8_t *bytes,
                               size_t length,
                               a1_mcu_image_header_t *header)
{
    uint32_t marker;
    if (bytes == NULL || header == NULL || length < A1_MCU_IMAGE_HEADER_SIZE) {
        return -1;
    }
    marker = read_le32(bytes);
    if (marker == A1_ERASED_WORD) {
        header->marker = A1_IMAGE_MARKER_OTA_PLACEHOLDER;
    } else if (marker == A1_BOOT_MAGIC) {
        header->marker = A1_IMAGE_MARKER_INSTALLED;
    } else {
        header->marker = A1_IMAGE_MARKER_INVALID;
        return -2;
    }
    header->format_word = read_le32(bytes + 4u);
    header->reserved_word = read_le32(bytes + 8u);
    header->build_info_address = read_le32(bytes + 12u);
    if (header->format_word != A1_IMAGE_FORMAT_WORD ||
        header->reserved_word != 0u ||
        header->build_info_address < UINT32_C(0x30000000) ||
        header->build_info_address >= UINT32_C(0x31000000)) {
        return -3;
    }
    return 0;
}

int a1_hifi_image_header_decode(const uint8_t *bytes,
                                size_t length,
                                a1_hifi_image_header_t *header)
{
    if (bytes == NULL || header == NULL ||
        length < A1_HIFI_IMAGE_HEADER_MIN_SIZE) {
        return -1;
    }
    header->outer_marker = read_le32(bytes);
    header->format_word = read_le32(bytes + 4u);
    header->boot_magic = read_le32(bytes + 8u);
    header->entry_address = read_le32(bytes + 12u);
    header->iram_start = read_le32(bytes + 16u);
    header->iram_end = read_le32(bytes + 20u);
    header->dram_start = read_le32(bytes + 24u);
    header->dram_end = read_le32(bytes + 28u);
    if (header->outer_marker != A1_ERASED_WORD ||
        header->format_word != A1_IMAGE_FORMAT_WORD ||
        header->boot_magic != A1_BOOT_MAGIC ||
        header->iram_start >= header->iram_end ||
        header->dram_start >= header->dram_end) {
        return -2;
    }
    return 0;
}

bool a1_hifi_image_header_matches_v168_map(
    const a1_hifi_image_header_t *header)
{
    return header != NULL &&
           header->entry_address == UINT32_C(0x100cd150) &&
           header->iram_start == UINT32_C(0x00880000) &&
           header->iram_end == UINT32_C(0x00900000) &&
           header->dram_start == UINT32_C(0x20880000) &&
           header->dram_end == UINT32_C(0x209c0000);
}
