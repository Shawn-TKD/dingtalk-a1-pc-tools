#include "a1/ble_link.h"

#include <string.h>

static int has_prefix(const char *value, size_t length, const char *prefix)
{
    size_t prefix_length = strlen(prefix);
    return value != NULL && length >= prefix_length &&
           memcmp(value, prefix, prefix_length) == 0;
}

void a1_ble_link_init(a1_ble_link_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

a1_ble_phone_model_t a1_ble_phone_model_classify(
    const char *model,
    size_t length)
{
    if (has_prefix(model, length, "iPhone")) {
        return A1_BLE_PHONE_IPHONE;
    }
    if (has_prefix(model, length, "iPad")) {
        return A1_BLE_PHONE_IPAD;
    }
    if (has_prefix(model, length, "Android")) {
        return A1_BLE_PHONE_ANDROID;
    }
    if (has_prefix(model, length, "Harmonyos")) {
        return A1_BLE_PHONE_HARMONYOS;
    }
    return A1_BLE_PHONE_OTHER;
}

int a1_ble_phone_model_initial_params(
    a1_ble_phone_model_t model,
    a1_ble_connection_params_t *params)
{
    if (params == NULL) {
        return -1;
    }
    if (model != A1_BLE_PHONE_IPHONE && model != A1_BLE_PHONE_IPAD) {
        return 0;
    }
    params->interval = 12u;
    params->latency = 0u;
    params->supervision_timeout = 400u;
    return 1;
}

int a1_ble_link_apply_event(
    a1_ble_link_state_t *state,
    a1_ble_link_event_t event,
    const uint8_t address[6],
    int peer_was_active,
    int usb_adb_mode,
    a1_ble_link_effect_t *effect)
{
    if (state == NULL || effect == NULL ||
        (event != A1_BLE_LINK_CONNECTED &&
         event != A1_BLE_LINK_DISCONNECTED)) {
        return -1;
    }
    memset(effect, 0, sizeof(*effect));

    if (event == A1_BLE_LINK_CONNECTED) {
        if (address == NULL) {
            return -1;
        }
        state->physical_connected = 1;
        memcpy(state->address, address, sizeof(state->address));
        effect->stop_advertising = 1;
        effect->signal_ble_alarm = 1;
        effect->display_state = 5u;
        return 0;
    }

    state->physical_connected = 0;
    state->logically_authenticated = 0;
    effect->close_wifi_ap = 1;
    effect->cancel_file_sync = 1;
    effect->notify_audio_disconnect = peer_was_active != 0;
    effect->start_advertising = usb_adb_mode == 0;
    effect->clear_peer = address != NULL;
    effect->display_state = 6u;
    if (effect->clear_peer) {
        memset(state->address, 0, sizeof(state->address));
    }
    return 0;
}
