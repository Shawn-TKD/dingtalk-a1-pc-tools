#ifndef A1_BOOT_IMAGE_H
#define A1_BOOT_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    A1_MCU_IMAGE_HEADER_SIZE = 16,
    A1_HIFI_IMAGE_HEADER_MIN_SIZE = 32
};

#define A1_BOOT_MAGIC UINT32_C(0xbe57ec1c)
#define A1_ERASED_WORD UINT32_C(0xffffffff)
#define A1_IMAGE_FORMAT_WORD UINT32_C(0x00050000)

typedef enum {
    A1_IMAGE_MARKER_INVALID = 0,
    A1_IMAGE_MARKER_OTA_PLACEHOLDER,
    A1_IMAGE_MARKER_INSTALLED
} a1_image_marker_state_t;

typedef struct {
    a1_image_marker_state_t marker;
    uint32_t format_word;
    uint32_t reserved_word;
    uint32_t build_info_address;
} a1_mcu_image_header_t;

typedef struct {
    uint32_t outer_marker;
    uint32_t format_word;
    uint32_t boot_magic;
    uint32_t entry_address;
    uint32_t iram_start;
    uint32_t iram_end;
    uint32_t dram_start;
    uint32_t dram_end;
} a1_hifi_image_header_t;

int a1_mcu_image_header_decode(const uint8_t *bytes,
                               size_t length,
                               a1_mcu_image_header_t *header);
int a1_hifi_image_header_decode(const uint8_t *bytes,
                                size_t length,
                                a1_hifi_image_header_t *header);
bool a1_hifi_image_header_matches_v168_map(
    const a1_hifi_image_header_t *header);

#endif
