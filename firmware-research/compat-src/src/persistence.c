#include "a1/persistence.h"

#include <string.h>

typedef struct {
    a1_audio_setting_key_t setting;
    const char *key;
} audio_key_t;

static const audio_key_t AUDIO_KEYS[] = {
    {A1_AUDIO_SETTING_DELETE_AFTER_UPLOAD,
     A1_PERSIST_AUDIO_DELETE_AFTER_UPLOAD},
    {A1_AUDIO_SETTING_MODE, A1_PERSIST_AUDIO_MODE},
    {A1_AUDIO_SETTING_AES, A1_PERSIST_AUDIO_AES},
    {A1_AUDIO_SETTING_INCOGNITO_MODE, A1_PERSIST_AUDIO_INCOGNITO},
    {A1_AUDIO_SETTING_FORCE_SYNC_INCOGNITO, A1_PERSIST_AUDIO_FORCE_SYNC},
    {A1_AUDIO_SETTING_AI_KEY_OPTION, A1_PERSIST_AUDIO_KEY_OPTION},
    {A1_AUDIO_SETTING_STREAM_RECORD, A1_PERSIST_AUDIO_STREAM_RECORD}
};

static int audio_value(
    const a1_audio_settings_t *settings,
    a1_audio_setting_key_t key)
{
    switch (key) {
    case A1_AUDIO_SETTING_DELETE_AFTER_UPLOAD:
        return settings->delete_after_upload;
    case A1_AUDIO_SETTING_MODE:
        return settings->mode;
    case A1_AUDIO_SETTING_AES:
        return settings->aes;
    case A1_AUDIO_SETTING_INCOGNITO_MODE:
        return settings->incognito_mode;
    case A1_AUDIO_SETTING_FORCE_SYNC_INCOGNITO:
        return settings->force_sync_incognito;
    case A1_AUDIO_SETTING_AI_KEY_OPTION:
        return settings->ai_key_option;
    case A1_AUDIO_SETTING_STREAM_RECORD:
        return settings->stream_record;
    default:
        return 0;
    }
}

int a1_persistence_load_audio(
    const a1_persistence_ops_t *ops,
    a1_audio_settings_t *settings)
{
    size_t index;
    if (ops == NULL || ops->read_int == NULL || settings == NULL) {
        return -1;
    }
    a1_audio_settings_init(settings);
    for (index = 0u; index < sizeof(AUDIO_KEYS) / sizeof(AUDIO_KEYS[0]); ++index) {
        int value;
        int result = ops->read_int(ops->context, AUDIO_KEYS[index].key, &value);
        if (result < 0) {
            return -2;
        }
        if (result == 0 &&
            a1_audio_setting_apply(settings, AUDIO_KEYS[index].setting, value) != 0) {
            return -3;
        }
    }
    return 0;
}

int a1_persistence_store_audio_update(
    const a1_persistence_ops_t *ops,
    const a1_audio_settings_t *update)
{
    size_t index;
    if (ops == NULL || ops->write_int == NULL || update == NULL) {
        return -1;
    }
    for (index = 0u; index < sizeof(AUDIO_KEYS) / sizeof(AUDIO_KEYS[0]); ++index) {
        a1_audio_setting_key_t setting = AUDIO_KEYS[index].setting;
        if ((update->present_mask & A1_AUDIO_SETTING_PRESENT(setting)) != 0u &&
            ops->write_int(ops->context, AUDIO_KEYS[index].key,
                           audio_value(update, setting)) != 0) {
            return -2;
        }
    }
    /* upload_stream is a live start/stop operation in stock firmware, not a
     * persisted property, so it is intentionally absent from AUDIO_KEYS. */
    return 0;
}

int a1_persistence_load_gray(
    const a1_persistence_ops_t *ops,
    a1_gray_switch_state_t *state)
{
    int value;
    int result;
    if (ops == NULL || ops->read_int == NULL || state == NULL) {
        return -1;
    }
    a1_gray_switch_init(state);
    result = ops->read_int(ops->context, A1_PERSIST_LOG_RECORD, &value);
    if (result < 0) return -2;
    if (result == 0) state->log_record = value == 1;
    result = ops->read_int(ops->context, A1_PERSIST_AUDIO_STREAM_RECORD, &value);
    if (result < 0) return -2;
    if (result == 0) state->stream_record = value == 1;
    result = ops->read_int(ops->context, A1_PERSIST_BLE_PARAMETER_AUTO, &value);
    if (result < 0) return -2;
    if (result == 0) state->ble_connection_parameter_auto = value == 1;
    result = ops->read_int(ops->context, A1_PERSIST_REMARK, &value);
    if (result < 0) return -2;
    if (result == 0) state->remark = value;
    return 0;
}

int a1_persistence_store_gray_update(
    const a1_persistence_ops_t *ops,
    const a1_gray_switch_update_t *update,
    const a1_gray_switch_state_t *resulting_state)
{
    if (ops == NULL || ops->write_int == NULL || update == NULL ||
        resulting_state == NULL) {
        return -1;
    }
    if (update->has_log_record &&
        ops->write_int(ops->context, A1_PERSIST_LOG_RECORD,
                       resulting_state->log_record ? 1 : 0) != 0) return -2;
    if (update->has_stream_record &&
        ops->write_int(ops->context, A1_PERSIST_AUDIO_STREAM_RECORD,
                       resulting_state->stream_record ? 1 : 0) != 0) return -2;
    if (update->has_ble_connection_parameter_auto &&
        ops->write_int(ops->context, A1_PERSIST_BLE_PARAMETER_AUTO,
                       resulting_state->ble_connection_parameter_auto ? 1 : 0) != 0) return -2;
    /* The stock set handler writes remark=0 when the field is absent. */
    if (ops->write_int(ops->context, A1_PERSIST_REMARK,
                       resulting_state->remark) != 0) return -2;
    return 0;
}

int a1_persistence_load_schedule(
    const a1_persistence_ops_t *ops,
    a1_schedule_t *schedule)
{
    uint8_t bytes[A1_SCHEDULE_FILE_HEADER_SIZE +
                  A1_SCHEDULE_MAX_ENTRIES * A1_SCHEDULE_FILE_ENTRY_SIZE];
    size_t written = 0u;
    size_t consumed = 0u;
    int result;
    if (ops == NULL || ops->read_blob == NULL || schedule == NULL) {
        return -1;
    }
    a1_schedule_init(schedule);
    result = ops->read_blob(ops->context, A1_SCHEDULE_STORAGE_PATH,
                            bytes, sizeof(bytes), &written);
    if (result == 1) {
        return 0;
    }
    if (result != 0) {
        return -2;
    }
    result = a1_schedule_decode(bytes, written, schedule, &consumed);
    return result == A1_SCHEDULE_OK && consumed == written ? 0 : -3;
}

int a1_persistence_store_schedule(
    const a1_persistence_ops_t *ops,
    const a1_schedule_t *schedule)
{
    uint8_t bytes[A1_SCHEDULE_FILE_HEADER_SIZE +
                  A1_SCHEDULE_MAX_ENTRIES * A1_SCHEDULE_FILE_ENTRY_SIZE];
    size_t written;
    if (ops == NULL || schedule == NULL) {
        return -1;
    }
    if (schedule->count == 0u) {
        return ops->remove_blob == NULL
            ? -1 : ops->remove_blob(ops->context, A1_SCHEDULE_STORAGE_PATH);
    }
    if (ops->write_blob == NULL ||
        a1_schedule_encode(schedule, bytes, sizeof(bytes), &written) !=
            A1_SCHEDULE_OK) {
        return -2;
    }
    return ops->write_blob(ops->context, A1_SCHEDULE_STORAGE_PATH,
                           bytes, written) == 0 ? 0 : -3;
}
