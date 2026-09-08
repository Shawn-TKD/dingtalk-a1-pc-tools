#ifndef A1_GRAY_SWITCH_H
#define A1_GRAY_SWITCH_H

#include <stdbool.h>
#include <stdint.h>

enum {
    A1_GRAY_SWITCH_COMMAND = 0x0137,
    A1_GRAY_REMARK_ENABLED_VALUE = 2
};

typedef struct {
    bool log_record;
    bool stream_record;
    bool ble_connection_parameter_auto;
    int remark;
} a1_gray_switch_state_t;

typedef struct {
    bool has_log_record;
    int log_record;
    bool has_stream_record;
    int stream_record;
    bool has_ble_connection_parameter_auto;
    int ble_connection_parameter_auto;
    bool has_remark;
    int remark;
} a1_gray_switch_update_t;

typedef struct {
    bool stream_record_changed;
    int stream_record_event_value;
} a1_gray_switch_effect_t;

typedef enum {
    A1_GRAY_CONTROL_INVALID = 0,
    A1_GRAY_CONTROL_REBOOT,
    A1_GRAY_CONTROL_SHUTDOWN,
    A1_GRAY_CONTROL_SHELL_COMMAND,
    A1_GRAY_CONTROL_ACK_ONLY,
    A1_GRAY_CONTROL_DELETE_OLDEST_RECORDING
} a1_gray_control_action_t;

typedef struct {
    uint32_t key;
    a1_gray_control_action_t action;
    bool respond_before_action;
    bool respond_after_action;
    const char *shell_command;
} a1_gray_control_plan_t;

void a1_gray_switch_init(a1_gray_switch_state_t *state);
void a1_gray_switch_apply(a1_gray_switch_state_t *state,
                          const a1_gray_switch_update_t *update,
                          a1_gray_switch_effect_t *effect);
bool a1_gray_switch_remark_enabled(const a1_gray_switch_state_t *state);

/* This only describes stock side effects. It never reboots, shuts down,
 * deletes a recording, or executes a shell command. */
a1_gray_control_plan_t a1_gray_control_plan(uint32_t key,
                                            const char *value);

#endif
