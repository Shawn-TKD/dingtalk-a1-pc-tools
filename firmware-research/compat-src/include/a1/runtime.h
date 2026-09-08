#ifndef A1_RUNTIME_H
#define A1_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "a1/platform.h"
#include "a1/router.h"

typedef enum {
    A1_AUDIO_IDLE,
    A1_AUDIO_RECORDING,
    A1_AUDIO_PAUSED,
    A1_AUDIO_VOICE_MEMO,
    A1_AUDIO_LIVE_STREAM
} a1_audio_state_t;

typedef struct a1_runtime {
    const a1_platform_t *platform;
    bool transport_connected;
    bool authenticated;
    bool logical_session_connected;
    bool ai_key_held;
    a1_audio_state_t audio_state;
} a1_runtime_t;

void a1_runtime_init(a1_runtime_t *runtime, const a1_platform_t *platform);
void a1_runtime_set_transport(a1_runtime_t *runtime, bool connected);
void a1_runtime_set_authenticated(a1_runtime_t *runtime, bool authenticated);
void a1_runtime_set_logical_session(a1_runtime_t *runtime, bool connected);

int a1_runtime_ai_key_long_press(a1_runtime_t *runtime);
int a1_runtime_ai_key_release(a1_runtime_t *runtime);
int a1_runtime_vibrate(a1_runtime_t *runtime, uint32_t duration_ms);

#endif
