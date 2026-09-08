#ifndef A1_WIRE_REQUESTS_H
#define A1_WIRE_REQUESTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "a1/audio.h"
#include "a1/gray_switch.h"
#include "a1/remark.h"
#include "a1/schedule.h"
#include "a1/wifi.h"

enum {
    A1_WIRE_TOKEN_LENGTH = 64,
    A1_WIRE_MODEL_CAPACITY = 64,
    A1_WIRE_SDK_VERSION_CAPACITY = 32,
    A1_WIRE_RAW_PATH_CAPACITY = 64,
    A1_WIRE_FIRMWARE_VERSION_CAPACITY = 64,
    A1_WIRE_VERIFY_CODE_CAPACITY = 96,
    A1_WIRE_FILE_ATTRS_CAPACITY = 128
};

typedef enum {
    A1_WIRE_OK = 0,
    A1_WIRE_INVALID_JSON = -1,
    A1_WIRE_MISSING_FIELD = -2,
    A1_WIRE_WRONG_TYPE = -3,
    A1_WIRE_INVALID_VALUE = -4,
    A1_WIRE_VALUE_TOO_LONG = -5,
    A1_WIRE_UNSUPPORTED = -6
} a1_wire_result_t;

typedef struct {
    int64_t did;
    char token[A1_WIRE_TOKEN_LENGTH + 1u];
    bool has_timestamp;
    int64_t timestamp;
    bool has_model;
    char model[A1_WIRE_MODEL_CAPACITY];
    bool has_sdk_version;
    char sdk_version[A1_WIRE_SDK_VERSION_CAPACITY];
} a1_wire_connect_request_t;

typedef struct {
    a1_audio_action_t action;
    a1_audio_settings_t settings;
} a1_wire_audio_request_t;

typedef enum {
    A1_VOICEPRINT_START = 1,
    A1_VOICEPRINT_STOP = 2
} a1_voiceprint_action_t;

typedef struct {
    uint64_t fid;
    uint32_t offset;
    uint32_t progress;
} a1_wire_file_sync_request_t;

typedef struct {
    uint64_t start_fid;
    uint64_t end_fid;
    int32_t recently;
} a1_wire_file_list_request_t;

typedef struct {
    int32_t key;
    bool has_value;
    int32_t value;
} a1_wire_system_control_request_t;

typedef struct {
    char path[A1_WIRE_RAW_PATH_CAPACITY];
    uint32_t offset;
} a1_wire_raw_transfer_request_t;

typedef enum {
    A1_WIRE_SCHEDULE_GET = 1,
    A1_WIRE_SCHEDULE_SET = 2
} a1_wire_schedule_action_t;

typedef struct {
    a1_wire_schedule_action_t action;
    bool params_present;
    bool truncated;
    a1_schedule_t schedule;
} a1_wire_schedule_request_t;

typedef enum {
    A1_WIRE_GRAY_GET = 1,
    A1_WIRE_GRAY_SET = 2,
    A1_WIRE_GRAY_CONTROL = 3
} a1_wire_gray_action_t;

typedef struct {
    a1_wire_gray_action_t action;
    a1_gray_switch_update_t update;
    uint32_t control_key;
    bool has_control_value;
    char control_value[64];
} a1_wire_gray_request_t;

typedef struct {
    bool has_new_version;
    char new_version[A1_WIRE_FIRMWARE_VERSION_CAPACITY];
} a1_wire_firmware_query_t;

typedef enum {
    A1_WIRE_FILE_HEADER_OK = 0,
    A1_WIRE_FILE_HEADER_INVALID_JSON = -1,
    A1_WIRE_FILE_HEADER_MISSING_SIZE = -2,
    A1_WIRE_FILE_HEADER_VERIFY_FIELD = -3,
    A1_WIRE_FILE_HEADER_ATTRS_FIELD = -4,
    A1_WIRE_FILE_HEADER_VERSION_FIELD = -5,
    A1_WIRE_FILE_HEADER_INVALID_VALUE = -6
} a1_wire_file_header_result_t;

typedef struct {
    uint32_t size;
    char verify_code[A1_WIRE_VERIFY_CODE_CAPACITY];
    uint32_t verify_hash;
    char attrs[A1_WIRE_FILE_ATTRS_CAPACITY];
    bool has_version;
    char version[A1_WIRE_FIRMWARE_VERSION_CAPACITY];
} a1_wire_file_header_request_t;

int a1_wire_decode_connect(
    const uint8_t *json,
    size_t length,
    a1_wire_connect_request_t *request);
int a1_wire_decode_empty_object(const uint8_t *json, size_t length);
int a1_wire_decode_audio(
    const uint8_t *json,
    size_t length,
    a1_wire_audio_request_t *request);
int a1_wire_decode_voiceprint(
    const uint8_t *json,
    size_t length,
    a1_voiceprint_action_t *action);
int a1_wire_decode_open_ap(
    const uint8_t *json,
    size_t length,
    a1_wifi_mode_t *mode);
int a1_wire_decode_file_sync(
    const uint8_t *json,
    size_t length,
    a1_wire_file_sync_request_t *request);
int a1_wire_decode_file_list(
    const uint8_t *json,
    size_t length,
    a1_wire_file_list_request_t *request);
int a1_wire_decode_file_fid(
    const uint8_t *json,
    size_t length,
    uint64_t *fid);
int a1_wire_decode_remark(
    const uint8_t *json,
    size_t length,
    a1_remark_t *remark);
int a1_wire_decode_system_control(
    const uint8_t *json,
    size_t length,
    a1_wire_system_control_request_t *request);
int a1_wire_decode_raw_transfer(
    const uint8_t *json,
    size_t length,
    a1_wire_raw_transfer_request_t *request);
int a1_wire_decode_schedule(
    const uint8_t *json,
    size_t length,
    a1_wire_schedule_request_t *request);
int a1_wire_decode_gray_switch(
    const uint8_t *json,
    size_t length,
    a1_wire_gray_request_t *request);
int a1_wire_decode_firmware_query(
    const uint8_t *json,
    size_t length,
    a1_wire_firmware_query_t *request);
int a1_wire_decode_file_header(
    const uint8_t *json,
    size_t length,
    a1_wire_file_header_request_t *request);

#endif
