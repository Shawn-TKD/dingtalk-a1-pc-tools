#ifndef A1_WIFI_H
#define A1_WIFI_H

#include <stddef.h>
#include <stdint.h>

#include "a1/platform.h"

#define A1_WIFI_SSID_CAPACITY 32u
#define A1_WIFI_PASSWORD_CAPACITY 9u
#define A1_WIFI_IP_CAPACITY 32u
#define A1_WIFI_URL_CAPACITY 64u
#define A1_WIFI_HTTP_PORT 80u
#define A1_WIFI_TCP_PORT 5922u

typedef enum {
    A1_WIFI_MODE_HTTP_AUDIO = 0,
    A1_WIFI_MODE_TCP_FILE = 1
} a1_wifi_mode_t;

typedef struct {
    int opened;
    a1_wifi_mode_t mode;
    uint16_t port;
    char ssid[A1_WIFI_SSID_CAPACITY];
    char password[A1_WIFI_PASSWORD_CAPACITY];
    char ip[A1_WIFI_IP_CAPACITY];
    char url[A1_WIFI_URL_CAPACITY];
} a1_wifi_state_t;

void a1_wifi_init(a1_wifi_state_t *state);

int a1_wifi_open(
    a1_wifi_state_t *state,
    const a1_platform_t *platform,
    a1_wifi_mode_t mode,
    const char *serial_number);

int a1_wifi_close(
    a1_wifi_state_t *state,
    const a1_platform_t *platform);

#endif
