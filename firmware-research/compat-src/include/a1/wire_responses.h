#ifndef A1_WIRE_RESPONSES_H
#define A1_WIRE_RESPONSES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "a1/audio.h"
#include "a1/device_info.h"
#include "a1/gray_switch.h"
#include "a1/schedule.h"
#include "a1/wifi.h"

typedef struct {
    bool remark;
    bool voiceprint;
    bool incognito_mode;
    bool ai_key_option;
    bool schedule;
} a1_wire_capabilities_t;

int a1_wire_encode_code(
    char *output,
    size_t capacity,
    uint16_t code,
    size_t *written);
int a1_wire_encode_code_with_type(
    char *output,
    size_t capacity,
    uint16_t code,
    uint32_t type,
    size_t *written);
int a1_wire_encode_random(
    char *output,
    size_t capacity,
    const char *random_hex,
    size_t *written);
int a1_wire_encode_connect(
    char *output,
    size_t capacity,
    uint16_t code,
    const a1_wire_capabilities_t *capabilities,
    size_t *written);
int a1_wire_encode_device_info(
    char *output,
    size_t capacity,
    uint16_t code,
    const a1_device_identity_t *identity,
    size_t *written);
int a1_wire_encode_audio_status(
    char *output,
    size_t capacity,
    uint16_t code,
    const a1_device_status_t *status,
    const char *version,
    size_t *written);
int a1_wire_encode_audio_settings(
    char *output,
    size_t capacity,
    uint16_t code,
    const a1_audio_settings_t *settings,
    size_t *written);
int a1_wire_encode_wifi(
    char *output,
    size_t capacity,
    uint16_t code,
    const a1_wifi_state_t *wifi,
    size_t *written);
int a1_wire_encode_system_control(
    char *output,
    size_t capacity,
    int32_t key,
    uint16_t code,
    bool include_battery,
    uint8_t battery_percent,
    size_t *written);
int a1_wire_encode_gray_switch(
    char *output,
    size_t capacity,
    const a1_gray_switch_state_t *state,
    size_t *written);
int a1_wire_encode_schedule(
    char *output,
    size_t capacity,
    uint16_t code,
    const a1_schedule_t *schedule,
    size_t *written);
int a1_wire_encode_firmware_query(
    char *output,
    size_t capacity,
    uint16_t code,
    bool upgrade,
    const char *current_version,
    uint32_t offset,
    size_t *written);

#endif
