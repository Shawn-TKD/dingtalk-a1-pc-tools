#ifndef A1_BLE_LINK_H
#define A1_BLE_LINK_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    A1_BLE_PHONE_UNKNOWN = 0,
    A1_BLE_PHONE_IPHONE = 1,
    A1_BLE_PHONE_IPAD = 2,
    A1_BLE_PHONE_ANDROID = 3,
    A1_BLE_PHONE_OTHER = 4,
    A1_BLE_PHONE_HARMONYOS = 5
} a1_ble_phone_model_t;

typedef enum {
    A1_BLE_LINK_CONNECTED = 0,
    A1_BLE_LINK_DISCONNECTED = 1
} a1_ble_link_event_t;

typedef struct {
    int physical_connected;
    int logically_authenticated;
    a1_ble_phone_model_t phone_model;
    uint8_t address[6];
} a1_ble_link_state_t;

typedef struct {
    int stop_advertising;
    int start_advertising;
    int close_wifi_ap;
    int cancel_file_sync;
    int notify_audio_disconnect;
    int signal_ble_alarm;
    int clear_peer;
    uint8_t display_state;
} a1_ble_link_effect_t;

typedef struct {
    uint16_t interval;
    uint16_t latency;
    uint16_t supervision_timeout;
} a1_ble_connection_params_t;

void a1_ble_link_init(a1_ble_link_state_t *state);

a1_ble_phone_model_t a1_ble_phone_model_classify(
    const char *model,
    size_t length);

int a1_ble_phone_model_initial_params(
    a1_ble_phone_model_t model,
    a1_ble_connection_params_t *params);

int a1_ble_link_apply_event(
    a1_ble_link_state_t *state,
    a1_ble_link_event_t event,
    const uint8_t address[6],
    int peer_was_active,
    int usb_adb_mode,
    a1_ble_link_effect_t *effect);

#endif
