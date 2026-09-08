#include "a1/audio.h"

#include "a1/runtime.h"

static int is_boolean(int value)
{
    return value == 0 || value == 1;
}

void a1_audio_settings_init(a1_audio_settings_t *settings)
{
    if (settings == NULL) {
        return;
    }
    settings->present_mask = 0;
    settings->upload_stream = 0;
    settings->delete_after_upload = 0;
    settings->mode = 0;
    settings->aes = 0;
    settings->incognito_mode = 0;
    settings->force_sync_incognito = 0;
    settings->ai_key_option = 0;
    settings->stream_record = 0;
}

int a1_audio_setting_validate(a1_audio_setting_key_t key, int value)
{
    switch (key) {
    case A1_AUDIO_SETTING_UPLOAD_STREAM:
        return value == 1 || value == 2;
    case A1_AUDIO_SETTING_DELETE_AFTER_UPLOAD:
    case A1_AUDIO_SETTING_AES:
    case A1_AUDIO_SETTING_INCOGNITO_MODE:
    case A1_AUDIO_SETTING_FORCE_SYNC_INCOGNITO:
    case A1_AUDIO_SETTING_STREAM_RECORD:
        return is_boolean(value);
    case A1_AUDIO_SETTING_MODE:
        return value >= 0 && value <= 5;
    case A1_AUDIO_SETTING_AI_KEY_OPTION:
        return value == 1000 || value == 1001;
    default:
        return 0;
    }
}

int a1_audio_setting_apply(
    a1_audio_settings_t *settings,
    a1_audio_setting_key_t key,
    int value)
{
    if (settings == NULL || !a1_audio_setting_validate(key, value)) {
        return -1;
    }

    switch (key) {
    case A1_AUDIO_SETTING_UPLOAD_STREAM:
        settings->upload_stream = value;
        break;
    case A1_AUDIO_SETTING_DELETE_AFTER_UPLOAD:
        settings->delete_after_upload = value;
        break;
    case A1_AUDIO_SETTING_MODE:
        settings->mode = value;
        break;
    case A1_AUDIO_SETTING_AES:
        settings->aes = value;
        break;
    case A1_AUDIO_SETTING_INCOGNITO_MODE:
        settings->incognito_mode = value;
        break;
    case A1_AUDIO_SETTING_FORCE_SYNC_INCOGNITO:
        settings->force_sync_incognito = value;
        break;
    case A1_AUDIO_SETTING_AI_KEY_OPTION:
        settings->ai_key_option = value;
        break;
    case A1_AUDIO_SETTING_STREAM_RECORD:
        settings->stream_record = value;
        break;
    default:
        return -1;
    }
    settings->present_mask |= A1_AUDIO_SETTING_PRESENT(key);
    return 0;
}

int a1_audio_apply_action(a1_runtime_t *runtime, a1_audio_action_t action)
{
    int result;

    if (runtime == NULL || runtime->platform == NULL) {
        return -1;
    }

    switch (action) {
    case A1_AUDIO_ACTION_START:
        if (runtime->audio_state != A1_AUDIO_IDLE ||
            runtime->platform->start_recording == NULL) {
            return -2;
        }
        result = runtime->platform->start_recording(runtime->platform->context);
        if (result == 0) {
            runtime->audio_state = A1_AUDIO_RECORDING;
        }
        return result;

    case A1_AUDIO_ACTION_STOP:
        if ((runtime->audio_state != A1_AUDIO_RECORDING &&
             runtime->audio_state != A1_AUDIO_PAUSED) ||
            runtime->platform->stop_recording == NULL) {
            return -2;
        }
        result = runtime->platform->stop_recording(runtime->platform->context);
        if (result == 0) {
            runtime->audio_state = A1_AUDIO_IDLE;
        }
        return result;

    case A1_AUDIO_ACTION_PAUSE:
        if (runtime->audio_state != A1_AUDIO_RECORDING ||
            runtime->platform->pause_recording == NULL) {
            return -2;
        }
        result = runtime->platform->pause_recording(runtime->platform->context);
        if (result == 0) {
            runtime->audio_state = A1_AUDIO_PAUSED;
        }
        return result;

    case A1_AUDIO_ACTION_RESUME:
        if (runtime->audio_state != A1_AUDIO_PAUSED ||
            runtime->platform->resume_recording == NULL) {
            return -2;
        }
        result = runtime->platform->resume_recording(runtime->platform->context);
        if (result == 0) {
            runtime->audio_state = A1_AUDIO_RECORDING;
        }
        return result;

    case A1_AUDIO_ACTION_SET:
    case A1_AUDIO_ACTION_GET:
    default:
        return -3;
    }
}
