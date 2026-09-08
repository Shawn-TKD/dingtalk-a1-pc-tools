#ifndef A1_SYSTEM_CONTROL_H
#define A1_SYSTEM_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    A1_SYSTEM_KEY_POWER_ACTION = 1,
    A1_SYSTEM_KEY_CLEAR_RECORDINGS = 2,
    A1_SYSTEM_KEY_REMOVE_OTA_PACKAGE = 3,
    A1_SYSTEM_KEY_REQUEST_OTA_EVENT = 4,
    A1_SYSTEM_KEY_LOG_RECORDING = 5,
    A1_SYSTEM_KEY_BATTERY_PERCENT = 10001
} a1_system_control_key_t;

typedef enum {
    A1_SYSTEM_ACTION_CLEAR_RECORDINGS,
    A1_SYSTEM_ACTION_REMOVE_OTA_PACKAGE,
    A1_SYSTEM_ACTION_REQUEST_OTA_EVENT_9,
    A1_SYSTEM_ACTION_DISABLE_LOG_RECORDING,
    A1_SYSTEM_ACTION_ENABLE_LOG_RECORDING,
    A1_SYSTEM_ACTION_START_LOG_RECORDING,
    A1_SYSTEM_ACTION_POWER
} a1_system_action_t;

#define A1_SYSTEM_MAX_ACTIONS 2u

typedef struct {
    uint16_t response_code;
    bool include_battery_percent;
    uint8_t battery_percent;
    size_t immediate_action_count;
    a1_system_action_t immediate_actions[A1_SYSTEM_MAX_ACTIONS];
    size_t after_response_action_count;
    a1_system_action_t after_response_actions[A1_SYSTEM_MAX_ACTIONS];
} a1_system_control_plan_t;

/* Produces the stock action order without executing platform operations.
 * Destructive work remains explicit, inspectable, and owned by the adapter. */
a1_system_control_plan_t a1_system_control_plan(
    int32_t key,
    bool has_value,
    int32_t value,
    uint8_t battery_percent);

#endif
