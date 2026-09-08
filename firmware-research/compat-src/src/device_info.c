#include "a1/device_info.h"

#include <limits.h>
#include <string.h>

void a1_device_identity_init(a1_device_identity_t *identity)
{
    if (identity != NULL) {
        memset(identity, 0, sizeof(*identity));
    }
}

void a1_device_status_init(a1_device_status_t *status)
{
    if (status != NULL) {
        memset(status, 0, sizeof(*status));
        status->audio_state = A1_REPORTED_AUDIO_IDLE;
    }
}

const char *a1_reported_audio_state_name(a1_reported_audio_state_t state)
{
    static const char *const NAMES[] = {
        "idle",
        "recording",
        "paused",
        "streaming",
        "rec_streaming"
    };

    if ((unsigned)state >= sizeof(NAMES) / sizeof(NAMES[0])) {
        return NULL;
    }
    return NAMES[(unsigned)state];
}

int a1_device_status_set_storage_blocks(
    a1_device_status_t *status,
    uint64_t fragment_size,
    uint64_t total_blocks,
    uint64_t available_blocks)
{
    uint64_t total_mib;
    uint64_t remaining_mib;

    if (status == NULL) {
        return -1;
    }

    total_mib = (fragment_size * total_blocks) >> 20;
    remaining_mib = (fragment_size * available_blocks) >> 20;
    if (total_mib > UINT32_MAX || remaining_mib > UINT32_MAX) {
        return -2;
    }

    status->storage_total_mib = (uint32_t)total_mib;
    status->storage_remaining_mib = (uint32_t)remaining_mib;
    status->has_storage = true;
    return 0;
}
