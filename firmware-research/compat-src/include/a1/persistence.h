#ifndef A1_PERSISTENCE_H
#define A1_PERSISTENCE_H

#include <stddef.h>
#include <stdint.h>

#include "a1/audio.h"
#include "a1/gray_switch.h"
#include "a1/schedule.h"

#define A1_PERSIST_AUDIO_MODE "persist.dt.aud.mode"
#define A1_PERSIST_AUDIO_AES "persist.dt.aud.aes"
#define A1_PERSIST_AUDIO_INCOGNITO "persist.dt.aud.incognitomode"
#define A1_PERSIST_AUDIO_FORCE_SYNC "persist.dt.aud.force_sync"
#define A1_PERSIST_AUDIO_KEY_OPTION "persist.dt.aud.key_option"
#define A1_PERSIST_AUDIO_DELETE_AFTER_UPLOAD "persist.dt.aud.del_after_upload"
#define A1_PERSIST_AUDIO_STREAM_RECORD "persist.dt.aud.stream_rec"
#define A1_PERSIST_LOG_RECORD "persist.dt.logrecord"
#define A1_PERSIST_BLE_PARAMETER_AUTO "persist.dt.ble.param_auto"
#define A1_PERSIST_REMARK "persist.dt.remark"
#define A1_SCHEDULE_STORAGE_PATH "/emmc/schedule/schedule.dat"

typedef struct {
    void *context;
    /* Reads return 0 when present, 1 when absent, and a negative value on I/O
     * failure. Blob reads set written only when present. */
    int (*read_int)(void *context, const char *key, int *value);
    int (*write_int)(void *context, const char *key, int value);
    int (*read_blob)(void *context, const char *path,
                     uint8_t *output, size_t capacity, size_t *written);
    int (*write_blob)(void *context, const char *path,
                      const uint8_t *bytes, size_t length);
    int (*remove_blob)(void *context, const char *path);
} a1_persistence_ops_t;

int a1_persistence_load_audio(
    const a1_persistence_ops_t *ops,
    a1_audio_settings_t *settings);
int a1_persistence_store_audio_update(
    const a1_persistence_ops_t *ops,
    const a1_audio_settings_t *update);
int a1_persistence_load_gray(
    const a1_persistence_ops_t *ops,
    a1_gray_switch_state_t *state);
int a1_persistence_store_gray_update(
    const a1_persistence_ops_t *ops,
    const a1_gray_switch_update_t *update,
    const a1_gray_switch_state_t *resulting_state);
int a1_persistence_load_schedule(
    const a1_persistence_ops_t *ops,
    a1_schedule_t *schedule);
int a1_persistence_store_schedule(
    const a1_persistence_ops_t *ops,
    const a1_schedule_t *schedule);

#endif
