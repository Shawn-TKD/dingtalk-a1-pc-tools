#ifndef A1_FIRMWARE_UPDATE_H
#define A1_FIRMWARE_UPDATE_H

#include <stdbool.h>
#include <stdint.h>

enum {
    A1_UPDATE_VERSION_CAPACITY = 64
};

typedef struct {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    uint64_t build;
} a1_firmware_version_t;

/* The compatible parser accepts the canonical version shape emitted by
 * V1.6.88: V<major>.<minor>.<patch>-<build>. */
int a1_firmware_version_parse(const char *text, a1_firmware_version_t *version);
bool a1_firmware_version_is_newer(const char *candidate, const char *current);

#endif
