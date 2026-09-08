#ifndef A1_AUDIO_H
#define A1_AUDIO_H

#include <stdint.h>

struct a1_runtime;

typedef enum {
    A1_AUDIO_ACTION_START = 1,
    A1_AUDIO_ACTION_STOP = 2,
    A1_AUDIO_ACTION_PAUSE = 3,
    A1_AUDIO_ACTION_RESUME = 4,
    A1_AUDIO_ACTION_SET = 5,
    A1_AUDIO_ACTION_GET = 6
} a1_audio_action_t;

typedef enum {
    A1_AUDIO_SETTING_UPLOAD_STREAM = 2,
    A1_AUDIO_SETTING_DELETE_AFTER_UPLOAD = 3,
    A1_AUDIO_SETTING_MODE = 5,
    A1_AUDIO_SETTING_AES = 6,
    A1_AUDIO_SETTING_INCOGNITO_MODE = 7,
    A1_AUDIO_SETTING_FORCE_SYNC_INCOGNITO = 8,
    A1_AUDIO_SETTING_AI_KEY_OPTION = 9,
    A1_AUDIO_SETTING_STREAM_RECORD = 10
} a1_audio_setting_key_t;

typedef struct {
    uint32_t present_mask;
    int upload_stream;
    int delete_after_upload;
    int mode;
    int aes;
    int incognito_mode;
    int force_sync_incognito;
    int ai_key_option;
    int stream_record;
} a1_audio_settings_t;

#define A1_AUDIO_SETTING_PRESENT(key) (1u << (uint32_t)(key))

void a1_audio_settings_init(a1_audio_settings_t *settings);
int a1_audio_setting_validate(a1_audio_setting_key_t key, int value);
int a1_audio_setting_apply(
    a1_audio_settings_t *settings,
    a1_audio_setting_key_t key,
    int value);
int a1_audio_apply_action(struct a1_runtime *runtime, a1_audio_action_t action);

#endif
