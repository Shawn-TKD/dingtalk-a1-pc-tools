#ifndef A1_PLATFORM_H
#define A1_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    A1_LOG_DEBUG,
    A1_LOG_INFO,
    A1_LOG_WARNING,
    A1_LOG_ERROR
} a1_log_level_t;

typedef struct {
    void *context;

    int (*send_response)(
        void *context,
        uint16_t command,
        uint8_t message_id,
        uint16_t code,
        const char *json_fields);

    /* Preferred by the compatible protocol service: body is a complete UTF-8
     * JSON object and remains owned by the caller for the duration of the call. */
    int (*send_json_response)(
        void *context,
        uint16_t command,
        uint8_t message_id,
        const char *body,
        size_t body_length);

    int (*start_live_stream)(void *context);
    int (*stop_live_stream)(void *context);
    int (*start_voice_memo)(void *context);
    int (*stop_voice_memo)(void *context);
    int (*start_recording)(void *context);
    int (*stop_recording)(void *context);
    int (*pause_recording)(void *context);
    int (*resume_recording)(void *context);

    int (*start_file_list)(
        void *context,
        uint64_t start_fid,
        uint64_t end_fid,
        int32_t recently,
        uint8_t message_id);
    int (*start_file_sync)(
        void *context,
        uint64_t fid,
        uint32_t offset,
        uint32_t progress,
        uint8_t message_id);
    int (*delete_recording)(void *context, uint64_t fid);
    int (*start_raw_transfer)(
        void *context,
        const char *path,
        uint32_t offset,
        uint8_t message_id);
    int (*cancel_raw_transfer)(void *context);

    int (*set_log_recording)(void *context, int enabled);
    int (*start_log_recording)(void *context);
    int (*clear_recordings)(void *context);
    int (*remove_ota_package)(void *context);
    int (*request_ota_event)(void *context, int event);
    int (*get_ota_resume_offset)(
        void *context,
        const char *target_version,
        uint32_t *offset);
    /* These callbacks only stage and verify an inbound file. Applying an OTA
     * image is deliberately a separate, absent capability. */
    int (*begin_inbound_file)(
        void *context,
        int type,
        uint32_t total_size,
        uint32_t resume_offset,
        uint32_t expected_verify_hash,
        const char *attrs,
        const char *version);
    int (*write_inbound_file)(
        void *context,
        const uint8_t *bytes,
        size_t length);
    int (*finish_inbound_file)(
        void *context,
        uint32_t *computed_verify_hash);
    int (*cancel_inbound_file)(void *context);
    int (*request_power_action)(void *context);
    int (*battery_percent)(void *context);
    int (*vibrate_ms)(void *context, uint32_t duration_ms);

    int (*wifi_create_ap)(
        void *context,
        int mode,
        const char *ssid,
        const char *password);
    int (*wifi_destroy_ap)(void *context);
    int (*wifi_get_ip)(void *context, char *output, size_t output_capacity);
    int (*cancel_file_sync)(void *context);
    int (*set_performance_lock)(void *context, int enabled);

    int (*random_bytes)(void *context, uint8_t *output, size_t length);
    int (*aes_128_cbc_encrypt)(
        void *context,
        const uint8_t key[16],
        const uint8_t iv[16],
        const uint8_t *input,
        size_t length,
        uint8_t *output);

    void (*log)(void *context, a1_log_level_t level, const char *message);
} a1_platform_t;

#endif
