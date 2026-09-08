#include "a1/gray_switch.h"

#include <string.h>

void a1_gray_switch_init(a1_gray_switch_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
        state->stream_record = true;
    }
}

void a1_gray_switch_apply(a1_gray_switch_state_t *state,
                          const a1_gray_switch_update_t *update,
                          a1_gray_switch_effect_t *effect)
{
    if (effect != NULL) {
        memset(effect, 0, sizeof(*effect));
    }
    if (state == NULL || update == NULL) {
        return;
    }
    if (update->has_log_record) {
        state->log_record = update->log_record == 1;
    }
    if (update->has_stream_record) {
        state->stream_record = update->stream_record == 1;
        if (effect != NULL) {
            effect->stream_record_changed = true;
            /* The stock gray-switch handler forwards 1 for enabled and 2 for
             * disabled to audio-event key 10. */
            effect->stream_record_event_value =
                state->stream_record ? 1 : 2;
        }
    }
    if (update->has_ble_connection_parameter_auto) {
        state->ble_connection_parameter_auto =
            update->ble_connection_parameter_auto == 1;
    }

    /* Stock V1.6.88 explicitly writes zero when `remark` is absent, unlike
     * the other optional set fields. Preserve that surprising behavior. */
    state->remark = update->has_remark ? update->remark : 0;
}

bool a1_gray_switch_remark_enabled(const a1_gray_switch_state_t *state)
{
    return state != NULL && state->remark == A1_GRAY_REMARK_ENABLED_VALUE;
}

a1_gray_control_plan_t a1_gray_control_plan(uint32_t key,
                                            const char *value)
{
    a1_gray_control_plan_t plan = {
        key, A1_GRAY_CONTROL_INVALID, false, false, NULL
    };

    switch (key) {
    case 1:
        plan.action = A1_GRAY_CONTROL_REBOOT;
        plan.respond_before_action = true;
        break;
    case 2:
        plan.action = A1_GRAY_CONTROL_SHUTDOWN;
        plan.respond_before_action = true;
        plan.shell_command = "shutdown";
        break;
    case 3:
        if (value != NULL) {
            plan.action = A1_GRAY_CONTROL_SHELL_COMMAND;
            plan.respond_before_action = true;
            plan.shell_command = value;
        }
        break;
    case 4:
        plan.action = A1_GRAY_CONTROL_ACK_ONLY;
        plan.respond_before_action = true;
        break;
    case 5:
        plan.action = A1_GRAY_CONTROL_DELETE_OLDEST_RECORDING;
        plan.respond_after_action = true;
        break;
    default:
        break;
    }
    return plan;
}
