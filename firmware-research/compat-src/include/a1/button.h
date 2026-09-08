#ifndef A1_BUTTON_H
#define A1_BUTTON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "a1/runtime.h"

/* Values passed by the stock button HAL to dt_audio_control_button_callback. */
typedef enum {
    A1_BUTTON_AI_DOWN = 0,
    A1_BUTTON_AI_CLICK = 1,
    A1_BUTTON_AI_UP = 2,
    A1_BUTTON_AI_LONG_PRESS = 3,
    A1_BUTTON_AI_DOUBLE_CLICK = 4,
    A1_BUTTON_RECORD_DOWN = 5,
    A1_BUTTON_RECORD_CLICK = 6,
    A1_BUTTON_RECORD_UP = 7,
    A1_BUTTON_RECORD_LONG_PRESS = 8,
    A1_BUTTON_RECORD_LONG_LONG_PRESS = 9
} a1_button_event_t;

enum {
    A1_BUTTON_AI_RELEASE_MIN_MS = 390,
    A1_BUTTON_RECORD_START_MIN_MS = 790,
    A1_BUTTON_SHUTDOWN_MIN_MS = 5000,
    A1_BUTTON_SHUTDOWN_PULSE_MS = 200,
    A1_BUTTON_SHUTDOWN_PULSE_COUNT = 4,
    A1_BUTTON_SHUTDOWN_PULSE_GAP_MS = 50,
    A1_BUTTON_SHUTDOWN_FINAL_PULSE_MS = 1000
};

typedef enum {
    A1_BUTTON_ACTION_DISPLAY_ACTIVITY,
    A1_BUTTON_ACTION_START_RECORDING,
    A1_BUTTON_ACTION_STOP_RECORDING,
    A1_BUTTON_ACTION_PAUSE_RECORDING,
    A1_BUTTON_ACTION_RESUME_RECORDING,
    A1_BUTTON_ACTION_ADD_MARKER,
    A1_BUTTON_ACTION_START_LIVE_STREAM,
    A1_BUTTON_ACTION_STOP_LIVE_STREAM,
    A1_BUTTON_ACTION_START_VOICE_MEMO,
    A1_BUTTON_ACTION_STOP_VOICE_MEMO,
    A1_BUTTON_ACTION_SHOW_RELEASE_TO_RECORD,
    A1_BUTTON_ACTION_SHOW_ASSOCIATION,
    A1_BUTTON_ACTION_VIBRATE_ONCE,
    A1_BUTTON_ACTION_VIBRATE_TWICE,
    A1_BUTTON_ACTION_BEGIN_SHUTDOWN_CONFIRMATION,
    A1_BUTTON_ACTION_CANCEL_SHUTDOWN,
    A1_BUTTON_ACTION_SHUTDOWN
} a1_button_action_t;

typedef enum {
    A1_BUTTON_HANDLED = 0,
    A1_BUTTON_IGNORED = 1,
    A1_BUTTON_INVALID = -1
} a1_button_result_t;

typedef struct {
    bool bound;
    bool ota_upgrade_active;
    bool usb_transfer_active;
    bool wifi_ap_active;
    bool voiceprint_upload_active;
    bool marker_enabled;
    int ai_key_option;
    bool logical_session_connected;
    a1_audio_state_t audio_state;
} a1_button_context_t;

typedef struct {
    uint64_t ai_down_ms;
    uint64_t record_down_ms;
    bool ai_down_valid;
    bool record_down_valid;
    bool ai_long_press_active;
    a1_audio_state_t ai_long_press_initial_state;
    bool shutdown_confirmation_active;
} a1_button_state_t;

#define A1_BUTTON_MAX_ACTIONS 4u

typedef struct {
    a1_button_result_t result;
    uint64_t press_duration_ms;
    size_t action_count;
    a1_button_action_t actions[A1_BUTTON_MAX_ACTIONS];
} a1_button_decision_t;

void a1_button_state_init(a1_button_state_t *state);

a1_button_decision_t a1_button_decide(
    a1_button_state_t *state,
    const a1_button_context_t *context,
    a1_button_event_t event,
    uint64_t observed_at_ms);

/* Called after the four short confirmation pulses have completed. A physical
 * RECORD_UP received before this call cancels the pending shutdown. */
a1_button_decision_t a1_button_finish_shutdown_confirmation(
    a1_button_state_t *state,
    a1_audio_state_t audio_state);

bool a1_button_decision_has(
    const a1_button_decision_t *decision,
    a1_button_action_t action);

#endif
