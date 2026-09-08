#include "a1/wifi.h"

#include <stdio.h>
#include <string.h>

static const char LETTERS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

void a1_wifi_init(a1_wifi_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

static int create_credentials(
    a1_wifi_state_t *state,
    const a1_platform_t *platform,
    const char *serial_number)
{
    uint8_t random[10];
    char suffix[7];
    size_t serial_length = serial_number == NULL ? 0u : strlen(serial_number);
    size_t index;

    if (platform->random_bytes(platform->context, random, sizeof(random)) != 0) {
        return -1;
    }
    suffix[0] = LETTERS[random[0] % 26u];
    suffix[1] = LETTERS[random[1] % 26u];
    if (serial_length >= 4u) {
        memcpy(suffix + 2u, serial_number + serial_length - 4u, 4u);
    } else {
        for (index = 0; index < 4u; ++index) {
            suffix[index + 2u] = LETTERS[random[index + 2u] % 26u];
        }
    }
    suffix[6] = '\0';
    if (snprintf(state->ssid, sizeof(state->ssid), "DingTalkA1_%s", suffix) < 0) {
        return -1;
    }
    for (index = 0; index < 8u; ++index) {
        state->password[index] = (char)('0' + random[index + 2u] % 10u);
    }
    state->password[8] = '\0';
    memset(random, 0, sizeof(random));
    return 0;
}

int a1_wifi_open(
    a1_wifi_state_t *state,
    const a1_platform_t *platform,
    a1_wifi_mode_t mode,
    const char *serial_number)
{
    int result;

    if (state == NULL || platform == NULL || platform->random_bytes == NULL ||
        platform->wifi_create_ap == NULL || platform->wifi_get_ip == NULL ||
        (mode != A1_WIFI_MODE_HTTP_AUDIO && mode != A1_WIFI_MODE_TCP_FILE)) {
        return -1;
    }
    if (state->opened) {
        return -2;
    }
    a1_wifi_init(state);
    result = create_credentials(state, platform, serial_number);
    if (result != 0) {
        return result;
    }
    result = platform->wifi_create_ap(
        platform->context, (int)mode, state->ssid, state->password);
    if (result != 0) {
        a1_wifi_init(state);
        return result;
    }

    state->mode = mode;
    state->port = mode == A1_WIFI_MODE_HTTP_AUDIO ? A1_WIFI_HTTP_PORT : A1_WIFI_TCP_PORT;
    if (platform->wifi_get_ip(platform->context, state->ip, sizeof(state->ip)) != 0) {
        memcpy(state->ip, "192.168.1.1", sizeof("192.168.1.1"));
    }
    if (mode == A1_WIFI_MODE_HTTP_AUDIO) {
        if (snprintf(state->url, sizeof(state->url), "http://%s/audio/", state->ip) < 0) {
            if (platform->wifi_destroy_ap != NULL) {
                platform->wifi_destroy_ap(platform->context);
            }
            a1_wifi_init(state);
            return -3;
        }
    }
    if (platform->cancel_file_sync != NULL) {
        platform->cancel_file_sync(platform->context);
    }
    if (platform->set_performance_lock != NULL) {
        platform->set_performance_lock(platform->context, 1);
    }
    state->opened = 1;
    return 0;
}

int a1_wifi_close(
    a1_wifi_state_t *state,
    const a1_platform_t *platform)
{
    int result;

    if (state == NULL || platform == NULL || platform->wifi_destroy_ap == NULL) {
        return -1;
    }
    if (!state->opened) {
        return 0;
    }
    result = platform->wifi_destroy_ap(platform->context);
    if (result != 0) {
        return result;
    }
    if (platform->set_performance_lock != NULL) {
        platform->set_performance_lock(platform->context, 0);
    }
    a1_wifi_init(state);
    return 0;
}
