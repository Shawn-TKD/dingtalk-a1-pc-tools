#ifndef A1_DEVICE_INFO_H
#define A1_DEVICE_INFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define A1_PRODUCT_KEY_CAPACITY 33u
#define A1_DEVICE_NAME_CAPACITY 33u
#define A1_WIFI_MAC_CAPACITY 18u
#define A1_FIRMWARE_VERSION_CAPACITY 32u

typedef enum {
    A1_REPORTED_AUDIO_IDLE = 0,
    A1_REPORTED_AUDIO_RECORDING = 1,
    A1_REPORTED_AUDIO_PAUSED = 2,
    A1_REPORTED_AUDIO_STREAMING = 3,
    A1_REPORTED_AUDIO_RECORDING_STREAMING = 4
} a1_reported_audio_state_t;

typedef struct {
    char product_key[A1_PRODUCT_KEY_CAPACITY];
    char device_name[A1_DEVICE_NAME_CAPACITY];
    char wifi_mac[A1_WIFI_MAC_CAPACITY];
    char firmware_version[A1_FIRMWARE_VERSION_CAPACITY];
    bool support_wifi;
    bool support_wire;
    bool support_gsm;
} a1_device_identity_t;

typedef struct {
    a1_reported_audio_state_t audio_state;
    uint32_t duration_ms;
    bool has_fid;
    uint64_t fid;
    bool has_storage;
    uint32_t storage_total_mib;
    uint32_t storage_remaining_mib;
    uint8_t battery_percent;
    uint8_t bind_status;
    uint8_t work_mode;
} a1_device_status_t;

void a1_device_identity_init(a1_device_identity_t *identity);
void a1_device_status_init(a1_device_status_t *status);

const char *a1_reported_audio_state_name(a1_reported_audio_state_t state);
int a1_device_status_set_storage_blocks(
    a1_device_status_t *status,
    uint64_t fragment_size,
    uint64_t total_blocks,
    uint64_t available_blocks);

#endif
