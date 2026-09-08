#include "a1/button.h"

#include <string.h>

static a1_button_decision_t decision(a1_button_result_t result)
{
    a1_button_decision_t value;
    memset(&value, 0, sizeof(value));
    value.result = result;
    return value;
}

static void add_action(a1_button_decision_t *value, a1_button_action_t action)
{
    if (value->action_count < A1_BUTTON_MAX_ACTIONS) {
        value->actions[value->action_count++] = action;
    }
}

static bool is_ordinary_recording(a1_audio_state_t state)
{
    return state == A1_AUDIO_RECORDING || state == A1_AUDIO_PAUSED;
}

static uint64_t elapsed(uint64_t started_at_ms, uint64_t observed_at_ms)
{
    return observed_at_ms >= started_at_ms ? observed_at_ms - started_at_ms : 0;
}

void a1_button_state_init(a1_button_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
        state->ai_long_press_initial_state = A1_AUDIO_IDLE;
    }
}

bool a1_button_decision_has(
    const a1_button_decision_t *value,
    a1_button_action_t action)
{
    size_t index;
    if (value == NULL) {
        return false;
    }
    for (index = 0; index < value->action_count; ++index) {
        if (value->actions[index] == action) {
            return true;
        }
    }
    return false;
}

a1_button_decision_t a1_button_decide(
    a1_button_state_t *state,
    const a1_button_context_t *context,
    a1_button_event_t event,
    uint64_t observed_at_ms)
{
    a1_button_decision_t value = decision(A1_BUTTON_HANDLED);

    if (state == NULL || context == NULL || event < A1_BUTTON_AI_DOWN ||
        event > A1_BUTTON_RECORD_LONG_LONG_PRESS) {
        return decision(A1_BUTTON_INVALID);
    }

    /* In stock firmware RECORD_UP can interrupt the blocking long-long-press
     * vibration loop from the HAL callback, before the queued event handler. */
    if (event == A1_BUTTON_RECORD_UP && state->shutdown_confirmation_active) {
        state->shutdown_confirmation_active = false;
        state->record_down_valid = false;
        add_action(&value, A1_BUTTON_ACTION_CANCEL_SHUTDOWN);
        return value;
    }

    if (context->ota_upgrade_active || context->usb_transfer_active ||
        context->wifi_ap_active ||
        (context->voiceprint_upload_active &&
         event != A1_BUTTON_RECORD_LONG_LONG_PRESS)) {
        return decision(A1_BUTTON_IGNORED);
    }

    switch (event) {
    case A1_BUTTON_AI_DOWN:
        state->ai_down_ms = observed_at_ms;
        state->ai_down_valid = true;
        state->ai_long_press_active = false;
        add_action(&value, A1_BUTTON_ACTION_DISPLAY_ACTIVITY);
        break;

    case A1_BUTTON_AI_CLICK:
        add_action(&value, A1_BUTTON_ACTION_DISPLAY_ACTIVITY);
        break;

    case A1_BUTTON_AI_UP:
        if (!context->bound) {
            return decision(A1_BUTTON_IGNORED);
        }
        if (state->ai_down_valid) {
            value.press_duration_ms = elapsed(state->ai_down_ms, observed_at_ms);
        }
        if (context->ai_key_option == 1001 && state->ai_long_press_active) {
            if (value.press_duration_ms >= A1_BUTTON_AI_RELEASE_MIN_MS &&
                state->ai_long_press_initial_state == A1_AUDIO_IDLE) {
                add_action(&value, A1_BUTTON_ACTION_START_RECORDING);
            }
        } else if (context->audio_state == A1_AUDIO_LIVE_STREAM) {
            add_action(&value, A1_BUTTON_ACTION_STOP_LIVE_STREAM);
        } else if (context->audio_state == A1_AUDIO_VOICE_MEMO) {
            add_action(&value, A1_BUTTON_ACTION_STOP_VOICE_MEMO);
        }
        state->ai_down_valid = false;
        state->ai_long_press_active = false;
        state->ai_long_press_initial_state = A1_AUDIO_IDLE;
        break;

    case A1_BUTTON_AI_LONG_PRESS:
        if (!context->bound) {
            return decision(A1_BUTTON_IGNORED);
        }
        if (context->ai_key_option == 1001) {
            state->ai_long_press_active = true;
            state->ai_long_press_initial_state = context->audio_state;
            if (context->audio_state == A1_AUDIO_IDLE) {
                add_action(&value, A1_BUTTON_ACTION_SHOW_RELEASE_TO_RECORD);
                add_action(&value, A1_BUTTON_ACTION_VIBRATE_ONCE);
            } else if (is_ordinary_recording(context->audio_state)) {
                add_action(&value, A1_BUTTON_ACTION_STOP_RECORDING);
                add_action(&value, A1_BUTTON_ACTION_VIBRATE_TWICE);
            } else {
                value.result = A1_BUTTON_IGNORED;
            }
        } else if (context->audio_state != A1_AUDIO_IDLE) {
            value.result = A1_BUTTON_IGNORED;
        } else if (context->logical_session_connected) {
            add_action(&value, A1_BUTTON_ACTION_START_LIVE_STREAM);
        } else {
            add_action(&value, A1_BUTTON_ACTION_START_VOICE_MEMO);
        }
        break;

    case A1_BUTTON_AI_DOUBLE_CLICK:
        if (context->audio_state == A1_AUDIO_PAUSED) {
            add_action(&value, A1_BUTTON_ACTION_RESUME_RECORDING);
            add_action(&value, A1_BUTTON_ACTION_VIBRATE_ONCE);
        } else if (context->audio_state == A1_AUDIO_RECORDING) {
            add_action(&value, A1_BUTTON_ACTION_PAUSE_RECORDING);
            add_action(&value, A1_BUTTON_ACTION_VIBRATE_ONCE);
        } else {
            add_action(&value, A1_BUTTON_ACTION_SHOW_ASSOCIATION);
        }
        break;

    case A1_BUTTON_RECORD_DOWN:
        state->record_down_ms = observed_at_ms;
        state->record_down_valid = true;
        add_action(&value, A1_BUTTON_ACTION_DISPLAY_ACTIVITY);
        if (!context->bound) {
            value.result = A1_BUTTON_IGNORED;
        }
        break;

    case A1_BUTTON_RECORD_CLICK:
        if (context->marker_enabled && is_ordinary_recording(context->audio_state)) {
            add_action(&value, A1_BUTTON_ACTION_ADD_MARKER);
        } else {
            value.result = A1_BUTTON_IGNORED;
        }
        break;

    case A1_BUTTON_RECORD_UP:
        if (!state->record_down_valid || !context->bound) {
            state->record_down_valid = false;
            return decision(A1_BUTTON_IGNORED);
        }
        value.press_duration_ms = elapsed(state->record_down_ms, observed_at_ms);
        state->record_down_valid = false;
        if (value.press_duration_ms >= A1_BUTTON_SHUTDOWN_MIN_MS) {
            if (is_ordinary_recording(context->audio_state)) {
                add_action(&value, A1_BUTTON_ACTION_STOP_RECORDING);
            }
            add_action(&value, A1_BUTTON_ACTION_SHUTDOWN);
        } else if (value.press_duration_ms >= A1_BUTTON_RECORD_START_MIN_MS) {
            if (context->audio_state == A1_AUDIO_IDLE) {
                add_action(&value, A1_BUTTON_ACTION_START_RECORDING);
            } else {
                value.result = A1_BUTTON_IGNORED;
            }
        } else {
            add_action(&value, A1_BUTTON_ACTION_DISPLAY_ACTIVITY);
        }
        break;

    case A1_BUTTON_RECORD_LONG_PRESS:
        if (!context->bound || context->audio_state == A1_AUDIO_LIVE_STREAM ||
            context->audio_state == A1_AUDIO_VOICE_MEMO) {
            return decision(A1_BUTTON_IGNORED);
        }
        if (is_ordinary_recording(context->audio_state)) {
            add_action(&value, A1_BUTTON_ACTION_STOP_RECORDING);
            add_action(&value, A1_BUTTON_ACTION_VIBRATE_TWICE);
        } else {
            add_action(&value, A1_BUTTON_ACTION_SHOW_RELEASE_TO_RECORD);
            add_action(&value, A1_BUTTON_ACTION_VIBRATE_ONCE);
        }
        break;

    case A1_BUTTON_RECORD_LONG_LONG_PRESS:
        state->shutdown_confirmation_active = true;
        add_action(&value, A1_BUTTON_ACTION_BEGIN_SHUTDOWN_CONFIRMATION);
        break;
    }

    return value;
}

a1_button_decision_t a1_button_finish_shutdown_confirmation(
    a1_button_state_t *state,
    a1_audio_state_t audio_state)
{
    a1_button_decision_t value = decision(A1_BUTTON_HANDLED);

    if (state == NULL) {
        return decision(A1_BUTTON_INVALID);
    }
    if (!state->shutdown_confirmation_active) {
        return decision(A1_BUTTON_IGNORED);
    }

    state->shutdown_confirmation_active = false;
    state->record_down_valid = false;
    if (is_ordinary_recording(audio_state)) {
        add_action(&value, A1_BUTTON_ACTION_STOP_RECORDING);
    }
    add_action(&value, A1_BUTTON_ACTION_SHUTDOWN);
    return value;
}
