#ifndef A1_FLASH_LAYOUT_H
#define A1_FLASH_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    A1_FLASH_PARTITION_COUNT = 12
};

#define A1_FLASH_TOTAL_SIZE UINT32_C(0x01000000)
#define A1_FLASH_PARTITION_UNIT UINT32_C(0x00000100)

typedef enum {
    A1_FLASH_EXECUTABLE = 1u << 0,
    A1_FLASH_UPDATE_STATE = 1u << 1,
    A1_FLASH_SYSTEM_STATE = 1u << 2,
    A1_FLASH_IDENTITY_SENSITIVE = 1u << 3,
    A1_FLASH_RECOVERY_CRITICAL = 1u << 4
} a1_flash_partition_flag_t;

typedef struct {
    const char *name;
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
} a1_flash_partition_t;

size_t a1_flash_partition_count(void);
const a1_flash_partition_t *a1_flash_partition_at(size_t index);
const a1_flash_partition_t *a1_flash_partition_find(const char *name);
bool a1_flash_partition_contains(const a1_flash_partition_t *partition,
                                 uint32_t offset,
                                 uint32_t length);
/* Validates the recovered built-in V1.6.88 geometry for bounds and overlap. */
int a1_flash_layout_validate(void);

#endif
