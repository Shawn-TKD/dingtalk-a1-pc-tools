#include "a1/system_control.h"

#include <string.h>

enum {
    A1_SYSTEM_CODE_OK = 200,
    A1_SYSTEM_CODE_INVALID_KEY = 408
};

static void add_immediate(
    a1_system_control_plan_t *plan,
    a1_system_action_t action)
{
    plan->immediate_actions[plan->immediate_action_count++] = action;
}

static void add_after_response(
    a1_system_control_plan_t *plan,
    a1_system_action_t action)
{
    plan->after_response_actions[plan->after_response_action_count++] = action;
}

a1_system_control_plan_t a1_system_control_plan(
    int32_t key,
    bool has_value,
    int32_t value,
    uint8_t battery_percent)
{
    a1_system_control_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    plan.response_code = A1_SYSTEM_CODE_OK;

    switch (key) {
    case A1_SYSTEM_KEY_POWER_ACTION:
        add_immediate(&plan, A1_SYSTEM_ACTION_POWER);
        break;

    case A1_SYSTEM_KEY_CLEAR_RECORDINGS:
        add_immediate(&plan, A1_SYSTEM_ACTION_CLEAR_RECORDINGS);
        add_after_response(&plan, A1_SYSTEM_ACTION_POWER);
        break;

    case A1_SYSTEM_KEY_REMOVE_OTA_PACKAGE:
        add_immediate(&plan, A1_SYSTEM_ACTION_REMOVE_OTA_PACKAGE);
        break;

    case A1_SYSTEM_KEY_REQUEST_OTA_EVENT:
        add_immediate(&plan, A1_SYSTEM_ACTION_REQUEST_OTA_EVENT_9);
        break;

    case A1_SYSTEM_KEY_LOG_RECORDING:
        if (has_value && value == 0) {
            add_immediate(&plan, A1_SYSTEM_ACTION_DISABLE_LOG_RECORDING);
        } else if (has_value && value == 1) {
            add_immediate(&plan, A1_SYSTEM_ACTION_ENABLE_LOG_RECORDING);
            add_immediate(&plan, A1_SYSTEM_ACTION_START_LOG_RECORDING);
        }
        break;

    case A1_SYSTEM_KEY_BATTERY_PERCENT:
        plan.include_battery_percent = true;
        plan.battery_percent = battery_percent > 100u ? 100u : battery_percent;
        break;

    default:
        plan.response_code = A1_SYSTEM_CODE_INVALID_KEY;
        break;
    }
    return plan;
}
