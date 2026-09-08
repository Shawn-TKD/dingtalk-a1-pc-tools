#include "a1/flash_layout.h"

#include <string.h>

static const a1_flash_partition_t PARTITIONS[A1_FLASH_PARTITION_COUNT] = {
    {"ota_b", UINT32_C(0x00040000), UINT32_C(0x000a0000),
     A1_FLASH_UPDATE_STATE | A1_FLASH_RECOVERY_CRITICAL},
    {"ota", UINT32_C(0x000e0000), UINT32_C(0x000a0000),
     A1_FLASH_UPDATE_STATE | A1_FLASH_RECOVERY_CRITICAL},
    {"ota_info", UINT32_C(0x00180000), UINT32_C(0x00008000),
     A1_FLASH_UPDATE_STATE | A1_FLASH_RECOVERY_CRITICAL},
    {"ota_flag", UINT32_C(0x00188000), UINT32_C(0x00008000),
     A1_FLASH_UPDATE_STATE | A1_FLASH_RECOVERY_CRITICAL},
    {"ap", UINT32_C(0x00190000), UINT32_C(0x00800000),
     A1_FLASH_EXECUTABLE | A1_FLASH_RECOVERY_CRITICAL},
    {"apc1", UINT32_C(0x00990000), UINT32_C(0x00300000),
     A1_FLASH_EXECUTABLE},
    {"hifi", UINT32_C(0x00c90000), UINT32_C(0x00170000),
     A1_FLASH_EXECUTABLE},
    {"user", UINT32_C(0x00e00000), UINT32_C(0x00100000),
     A1_FLASH_EXECUTABLE},
    {"misc", UINT32_C(0x00fe2000), UINT32_C(0x00001000),
     A1_FLASH_SYSTEM_STATE},
    {"bootinfo", UINT32_C(0x00fe3000), UINT32_C(0x00001000),
     A1_FLASH_SYSTEM_STATE | A1_FLASH_RECOVERY_CRITICAL},
    {"bes_reserved", UINT32_C(0x00fe4000), UINT32_C(0x0001b000),
     A1_FLASH_SYSTEM_STATE | A1_FLASH_RECOVERY_CRITICAL},
    {"factory", UINT32_C(0x00fff000), UINT32_C(0x00001000),
     A1_FLASH_SYSTEM_STATE | A1_FLASH_IDENTITY_SENSITIVE |
         A1_FLASH_RECOVERY_CRITICAL}
};

size_t a1_flash_partition_count(void)
{
    return A1_FLASH_PARTITION_COUNT;
}

const a1_flash_partition_t *a1_flash_partition_at(size_t index)
{
    return index < A1_FLASH_PARTITION_COUNT ? &PARTITIONS[index] : NULL;
}

const a1_flash_partition_t *a1_flash_partition_find(const char *name)
{
    size_t index;
    if (name == NULL) {
        return NULL;
    }
    for (index = 0u; index < A1_FLASH_PARTITION_COUNT; ++index) {
        if (strcmp(PARTITIONS[index].name, name) == 0) {
            return &PARTITIONS[index];
        }
    }
    return NULL;
}

bool a1_flash_partition_contains(const a1_flash_partition_t *partition,
                                 uint32_t offset,
                                 uint32_t length)
{
    uint64_t range_end;
    uint64_t partition_end;
    if (partition == NULL) {
        return false;
    }
    range_end = (uint64_t)offset + length;
    partition_end = (uint64_t)partition->offset + partition->size;
    return offset >= partition->offset && range_end <= partition_end;
}

int a1_flash_layout_validate(void)
{
    size_t left;
    size_t right;
    for (left = 0u; left < A1_FLASH_PARTITION_COUNT; ++left) {
        uint64_t left_end =
            (uint64_t)PARTITIONS[left].offset + PARTITIONS[left].size;
        if (PARTITIONS[left].name[0] == '\0' || PARTITIONS[left].size == 0u ||
            left_end > A1_FLASH_TOTAL_SIZE) {
            return -1;
        }
        for (right = left + 1u; right < A1_FLASH_PARTITION_COUNT; ++right) {
            uint64_t right_end =
                (uint64_t)PARTITIONS[right].offset + PARTITIONS[right].size;
            if ((uint64_t)PARTITIONS[left].offset < right_end &&
                (uint64_t)PARTITIONS[right].offset < left_end) {
                return -2;
            }
        }
    }
    return 0;
}
