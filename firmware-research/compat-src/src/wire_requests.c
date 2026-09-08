#include "a1/wire_requests.h"

#include <limits.h>
#include <string.h>

#include "a1/auth.h"
#include "a1/json_reader.h"

static int get_field(
    const uint8_t *json,
    size_t length,
    const char *key,
    a1_json_value_t *value,
    bool required)
{
    int result = a1_json_object_get(json, length, key, value);
    if (result < 0) {
        return A1_WIRE_INVALID_JSON;
    }
    if (result > 0) {
        return required ? A1_WIRE_MISSING_FIELD : 1;
    }
    return A1_WIRE_OK;
}

static int get_string(
    const uint8_t *json,
    size_t length,
    const char *key,
    char *output,
    size_t capacity,
    bool required,
    bool *present)
{
    a1_json_value_t value;
    int result = get_field(json, length, key, &value, required);
    if (result != A1_WIRE_OK) {
        if (result == 1 && present != NULL) {
            *present = false;
        }
        return result;
    }
    if (value.type != A1_JSON_STRING) {
        return A1_WIRE_WRONG_TYPE;
    }
    result = a1_json_string_copy(&value, output, capacity);
    if (result == -3) {
        return A1_WIRE_VALUE_TOO_LONG;
    }
    if (result != 0) {
        return A1_WIRE_INVALID_VALUE;
    }
    if (present != NULL) {
        *present = true;
    }
    return A1_WIRE_OK;
}

static int get_int64_field(
    const uint8_t *json,
    size_t length,
    const char *key,
    int64_t *output,
    bool required,
    bool string_allowed,
    bool *present)
{
    a1_json_value_t value;
    int result = get_field(json, length, key, &value, required);
    if (result != A1_WIRE_OK) {
        if (result == 1 && present != NULL) {
            *present = false;
        }
        return result;
    }
    if (value.type != A1_JSON_NUMBER &&
        !(string_allowed && value.type == A1_JSON_STRING)) {
        return A1_WIRE_WRONG_TYPE;
    }
    if (a1_json_int64(&value, output) != 0) {
        return A1_WIRE_INVALID_VALUE;
    }
    if (present != NULL) {
        *present = true;
    }
    return A1_WIRE_OK;
}

static int get_uint64_field(
    const uint8_t *json,
    size_t length,
    const char *key,
    uint64_t *output,
    bool required,
    bool string_allowed,
    bool *present)
{
    a1_json_value_t value;
    int result = get_field(json, length, key, &value, required);
    if (result != A1_WIRE_OK) {
        if (result == 1 && present != NULL) {
            *present = false;
        }
        return result;
    }
    if (value.type != A1_JSON_NUMBER &&
        !(string_allowed && value.type == A1_JSON_STRING)) {
        return A1_WIRE_WRONG_TYPE;
    }
    if (a1_json_uint64(&value, output) != 0) {
        return A1_WIRE_INVALID_VALUE;
    }
    if (present != NULL) {
        *present = true;
    }
    return A1_WIRE_OK;
}

static int string_equals(const a1_json_value_t *value, const char *text)
{
    size_t length = strlen(text);
    return value->type == A1_JSON_STRING && value->length == length &&
           memcmp(value->bytes, text, length) == 0;
}

int a1_wire_decode_connect(
    const uint8_t *json,
    size_t length,
    a1_wire_connect_request_t *request)
{
    a1_json_value_t did;
    int result;

    if (request == NULL) {
        return A1_WIRE_INVALID_VALUE;
    }
    memset(request, 0, sizeof(*request));
    result = get_field(json, length, "did", &did, true);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (did.type != A1_JSON_STRING) {
        return A1_WIRE_WRONG_TYPE;
    }
    if (a1_json_int64(&did, &request->did) != 0) {
        return A1_WIRE_INVALID_VALUE;
    }
    result = get_string(json, length, "token", request->token,
                        sizeof(request->token), true, NULL);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (strlen(request->token) != A1_WIRE_TOKEN_LENGTH) {
        return A1_WIRE_INVALID_VALUE;
    }
    result = get_int64_field(json, length, "timestamp", &request->timestamp,
                             false, true, &request->has_timestamp);
    if (result != A1_WIRE_OK && result != 1) {
        return result;
    }
    result = get_string(json, length, "model", request->model,
                        sizeof(request->model), false, &request->has_model);
    if (result != A1_WIRE_OK && result != 1) {
        return result;
    }
    result = get_string(json, length, "sdk_ver", request->sdk_version,
                        sizeof(request->sdk_version), false,
                        &request->has_sdk_version);
    if (result != A1_WIRE_OK && result != 1) {
        return result;
    }
    return A1_WIRE_OK;
}

int a1_wire_decode_empty_object(const uint8_t *json, size_t length)
{
    return a1_json_validate_object(json, length) == 0
        ? A1_WIRE_OK
        : A1_WIRE_INVALID_JSON;
}

static int setting_key(const a1_json_value_t *value, a1_audio_setting_key_t *key)
{
    if (string_equals(value, "upload_stream")) {
        *key = A1_AUDIO_SETTING_UPLOAD_STREAM;
    } else if (string_equals(value, "delete_after_upload")) {
        *key = A1_AUDIO_SETTING_DELETE_AFTER_UPLOAD;
    } else if (string_equals(value, "mode")) {
        *key = A1_AUDIO_SETTING_MODE;
    } else if (string_equals(value, "aes")) {
        *key = A1_AUDIO_SETTING_AES;
    } else if (string_equals(value, "incognitomode")) {
        *key = A1_AUDIO_SETTING_INCOGNITO_MODE;
    } else if (string_equals(value, "force_sync_incognito")) {
        *key = A1_AUDIO_SETTING_FORCE_SYNC_INCOGNITO;
    } else if (string_equals(value, "aikey_option")) {
        *key = A1_AUDIO_SETTING_AI_KEY_OPTION;
    } else if (string_equals(value, "stream_record")) {
        *key = A1_AUDIO_SETTING_STREAM_RECORD;
    } else {
        return A1_WIRE_UNSUPPORTED;
    }
    return A1_WIRE_OK;
}

static int decode_audio_settings(
    const a1_json_value_t *params,
    a1_audio_settings_t *settings)
{
    size_t count;
    size_t index;
    int result;

    if (params->type != A1_JSON_ARRAY) {
        return A1_WIRE_WRONG_TYPE;
    }
    if (a1_json_array_count(params, &count) != 0) {
        return A1_WIRE_INVALID_JSON;
    }
    a1_audio_settings_init(settings);
    for (index = 0u; index < count; ++index) {
        a1_json_value_t item;
        a1_json_value_t key_value;
        int64_t raw_value;
        a1_audio_setting_key_t key;
        if (a1_json_array_get(params, index, &item) != 0 ||
            item.type != A1_JSON_OBJECT) {
            return A1_WIRE_WRONG_TYPE;
        }
        result = get_field(item.bytes, item.length, "key", &key_value, true);
        if (result != A1_WIRE_OK) {
            return result;
        }
        if (key_value.type != A1_JSON_STRING) {
            return A1_WIRE_WRONG_TYPE;
        }
        result = setting_key(&key_value, &key);
        if (result != A1_WIRE_OK) {
            return result;
        }
        result = get_int64_field(item.bytes, item.length, "val", &raw_value,
                                 true, false, NULL);
        if (result != A1_WIRE_OK) {
            return result;
        }
        if (raw_value < INT_MIN || raw_value > INT_MAX ||
            a1_audio_setting_apply(settings, key, (int)raw_value) != 0) {
            return A1_WIRE_INVALID_VALUE;
        }
    }
    return A1_WIRE_OK;
}

int a1_wire_decode_audio(
    const uint8_t *json,
    size_t length,
    a1_wire_audio_request_t *request)
{
    a1_json_value_t action;
    a1_json_value_t params;
    int result;

    if (request == NULL) {
        return A1_WIRE_INVALID_VALUE;
    }
    memset(request, 0, sizeof(*request));
    result = get_field(json, length, "action", &action, true);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (action.type != A1_JSON_STRING) {
        return A1_WIRE_WRONG_TYPE;
    }
    if (string_equals(&action, "start")) {
        request->action = A1_AUDIO_ACTION_START;
    } else if (string_equals(&action, "stop")) {
        request->action = A1_AUDIO_ACTION_STOP;
    } else if (string_equals(&action, "pause")) {
        request->action = A1_AUDIO_ACTION_PAUSE;
    } else if (string_equals(&action, "resume")) {
        request->action = A1_AUDIO_ACTION_RESUME;
    } else if (string_equals(&action, "get")) {
        request->action = A1_AUDIO_ACTION_GET;
    } else if (string_equals(&action, "set")) {
        request->action = A1_AUDIO_ACTION_SET;
        result = get_field(json, length, "params", &params, true);
        if (result != A1_WIRE_OK) {
            return result;
        }
        return decode_audio_settings(&params, &request->settings);
    } else {
        return A1_WIRE_UNSUPPORTED;
    }
    a1_audio_settings_init(&request->settings);
    return A1_WIRE_OK;
}

int a1_wire_decode_voiceprint(
    const uint8_t *json,
    size_t length,
    a1_voiceprint_action_t *action)
{
    a1_json_value_t value;
    int result;

    if (action == NULL) {
        return A1_WIRE_INVALID_VALUE;
    }
    result = get_field(json, length, "action", &value, true);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (value.type != A1_JSON_STRING) {
        return A1_WIRE_WRONG_TYPE;
    }
    if (string_equals(&value, "start")) {
        *action = A1_VOICEPRINT_START;
    } else if (string_equals(&value, "stop")) {
        *action = A1_VOICEPRINT_STOP;
    } else {
        return A1_WIRE_UNSUPPORTED;
    }
    return A1_WIRE_OK;
}

int a1_wire_decode_open_ap(
    const uint8_t *json,
    size_t length,
    a1_wifi_mode_t *mode)
{
    int64_t value = 0;
    bool present = false;
    int result;

    if (mode == NULL) {
        return A1_WIRE_INVALID_VALUE;
    }
    if (a1_json_validate_object(json, length) != 0) {
        return A1_WIRE_INVALID_JSON;
    }
    result = get_int64_field(json, length, "type", &value, false, false, &present);
    if (result != A1_WIRE_OK && result != 1) {
        return result;
    }
    if (!present) {
        value = 0;
    }
    if (value != A1_WIFI_MODE_HTTP_AUDIO && value != A1_WIFI_MODE_TCP_FILE) {
        return A1_WIRE_INVALID_VALUE;
    }
    *mode = (a1_wifi_mode_t)value;
    return A1_WIRE_OK;
}

int a1_wire_decode_file_sync(
    const uint8_t *json,
    size_t length,
    a1_wire_file_sync_request_t *request)
{
    uint64_t value;
    int result;

    if (request == NULL) {
        return A1_WIRE_INVALID_VALUE;
    }
    memset(request, 0, sizeof(*request));
    result = get_uint64_field(json, length, "fid", &request->fid,
                              true, true, NULL);
    if (result != A1_WIRE_OK) {
        return result;
    }
    result = get_uint64_field(json, length, "offset", &value,
                              true, false, NULL);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (value > UINT32_MAX) {
        return A1_WIRE_INVALID_VALUE;
    }
    request->offset = (uint32_t)value;
    result = get_uint64_field(json, length, "progress", &value,
                              true, false, NULL);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (value > UINT32_MAX) {
        return A1_WIRE_INVALID_VALUE;
    }
    request->progress = (uint32_t)value;
    return A1_WIRE_OK;
}

int a1_wire_decode_file_list(
    const uint8_t *json,
    size_t length,
    a1_wire_file_list_request_t *request)
{
    int64_t recently = -1;
    bool present = false;
    int result;

    if (request == NULL) {
        return A1_WIRE_INVALID_VALUE;
    }
    memset(request, 0, sizeof(*request));
    request->recently = -1;
    result = get_uint64_field(json, length, "s_fid", &request->start_fid,
                              true, true, NULL);
    if (result != A1_WIRE_OK) {
        return result;
    }
    result = get_uint64_field(json, length, "e_fid", &request->end_fid,
                              true, true, NULL);
    if (result != A1_WIRE_OK) {
        return result;
    }
    result = get_int64_field(json, length, "recently", &recently,
                             false, false, &present);
    if (result != A1_WIRE_OK && result != 1) {
        return result;
    }
    if (present) {
        if (recently < 1 || recently > 300) {
            return A1_WIRE_INVALID_VALUE;
        }
        request->recently = (int32_t)recently;
    }
    return A1_WIRE_OK;
}

int a1_wire_decode_file_fid(
    const uint8_t *json,
    size_t length,
    uint64_t *fid)
{
    return fid == NULL
        ? A1_WIRE_INVALID_VALUE
        : get_uint64_field(json, length, "fid", fid, true, true, NULL);
}

int a1_wire_decode_remark(
    const uint8_t *json,
    size_t length,
    a1_remark_t *remark)
{
    uint64_t type;
    int result;

    if (remark == NULL) {
        return A1_WIRE_INVALID_VALUE;
    }
    memset(remark, 0, sizeof(*remark));
    result = get_uint64_field(json, length, "fid", &remark->fid,
                              true, true, NULL);
    if (result != A1_WIRE_OK) {
        return result;
    }
    result = get_uint64_field(json, length, "ts", &remark->timestamp_seconds,
                              true, true, NULL);
    if (result != A1_WIRE_OK) {
        return result;
    }
    result = get_uint64_field(json, length, "type", &type,
                              true, false, NULL);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (type > UINT32_MAX) {
        return A1_WIRE_INVALID_VALUE;
    }
    remark->type = (uint32_t)type;
    return A1_WIRE_OK;
}

int a1_wire_decode_system_control(
    const uint8_t *json,
    size_t length,
    a1_wire_system_control_request_t *request)
{
    int64_t value;
    int result;

    if (request == NULL) {
        return A1_WIRE_INVALID_VALUE;
    }
    memset(request, 0, sizeof(*request));
    result = get_int64_field(json, length, "key", &value,
                             true, false, NULL);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (value < INT32_MIN || value > INT32_MAX) {
        return A1_WIRE_INVALID_VALUE;
    }
    request->key = (int32_t)value;
    result = get_int64_field(json, length, "val", &value,
                             false, false, &request->has_value);
    if (result != A1_WIRE_OK && result != 1) {
        return result;
    }
    if (request->has_value) {
        if (value < INT32_MIN || value > INT32_MAX) {
            return A1_WIRE_INVALID_VALUE;
        }
        request->value = (int32_t)value;
    } else {
        request->value = -1;
    }
    return A1_WIRE_OK;
}

int a1_wire_decode_raw_transfer(
    const uint8_t *json,
    size_t length,
    a1_wire_raw_transfer_request_t *request)
{
    uint64_t offset = 0u;
    bool present = false;
    int result;

    if (request == NULL) {
        return A1_WIRE_INVALID_VALUE;
    }
    memset(request, 0, sizeof(*request));
    result = get_string(json, length, "path", request->path,
                        sizeof(request->path), true, NULL);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (request->path[0] == '\0') {
        return A1_WIRE_INVALID_VALUE;
    }
    result = get_uint64_field(json, length, "offset", &offset,
                              false, false, &present);
    if (result != A1_WIRE_OK && result != 1) {
        return result;
    }
    if (present && offset > UINT32_MAX) {
        return A1_WIRE_INVALID_VALUE;
    }
    request->offset = present ? (uint32_t)offset : 0u;
    return A1_WIRE_OK;
}

int a1_wire_decode_firmware_query(
    const uint8_t *json,
    size_t length,
    a1_wire_firmware_query_t *request)
{
    a1_json_value_t value;
    int result;
    if (request == NULL) {
        return A1_WIRE_INVALID_VALUE;
    }
    memset(request, 0, sizeof(*request));
    if (a1_json_validate_object(json, length) != 0) {
        return A1_WIRE_INVALID_JSON;
    }
    result = a1_json_object_get(json, length, "new_ver", &value);
    if (result < 0) {
        return A1_WIRE_INVALID_JSON;
    }
    /* V1.6.88 logs a missing or non-string new_ver but still replies with
     * upgrade=false, so this decoder intentionally preserves that behavior. */
    if (result > 0 || value.type != A1_JSON_STRING) {
        return A1_WIRE_OK;
    }
    result = a1_json_string_copy(&value, request->new_version,
                                 sizeof(request->new_version));
    if (result == -3) {
        return A1_WIRE_VALUE_TOO_LONG;
    }
    if (result != 0) {
        return A1_WIRE_INVALID_VALUE;
    }
    request->has_new_version = true;
    return A1_WIRE_OK;
}

int a1_wire_decode_file_header(
    const uint8_t *json,
    size_t length,
    a1_wire_file_header_request_t *request)
{
    uint64_t size;
    int result;
    if (request == NULL) {
        return A1_WIRE_FILE_HEADER_INVALID_VALUE;
    }
    memset(request, 0, sizeof(*request));
    if (a1_json_validate_object(json, length) != 0) {
        return A1_WIRE_FILE_HEADER_INVALID_JSON;
    }
    result = get_uint64_field(json, length, "size", &size,
                              true, false, NULL);
    if (result == A1_WIRE_MISSING_FIELD) {
        return A1_WIRE_FILE_HEADER_MISSING_SIZE;
    }
    if (result != A1_WIRE_OK || size == 0u || size > UINT32_MAX) {
        return A1_WIRE_FILE_HEADER_INVALID_VALUE;
    }
    request->size = (uint32_t)size;
    result = get_string(json, length, "verify_code", request->verify_code,
                        sizeof(request->verify_code), true, NULL);
    if (result != A1_WIRE_OK) {
        return A1_WIRE_FILE_HEADER_VERIFY_FIELD;
    }
    request->verify_hash = a1_djb2_bytes(
        (const uint8_t *)request->verify_code, strlen(request->verify_code));
    result = get_string(json, length, "attrs", request->attrs,
                        sizeof(request->attrs), true, NULL);
    if (result != A1_WIRE_OK) {
        return A1_WIRE_FILE_HEADER_ATTRS_FIELD;
    }
    result = get_string(json, length, "version", request->version,
                        sizeof(request->version), false,
                        &request->has_version);
    if (result != A1_WIRE_OK && result != 1) {
        return A1_WIRE_FILE_HEADER_VERSION_FIELD;
    }
    return A1_WIRE_FILE_HEADER_OK;
}

int a1_wire_decode_schedule(
    const uint8_t *json,
    size_t length,
    a1_wire_schedule_request_t *request)
{
    a1_json_value_t action;
    a1_json_value_t params;
    uint64_t value;
    size_t count;
    size_t index;
    int result;

    if (request == NULL) {
        return A1_WIRE_INVALID_VALUE;
    }
    memset(request, 0, sizeof(*request));
    result = get_field(json, length, "action", &action, true);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (action.type != A1_JSON_STRING) {
        return A1_WIRE_WRONG_TYPE;
    }
    if (string_equals(&action, "get")) {
        request->action = A1_WIRE_SCHEDULE_GET;
        return A1_WIRE_OK;
    }
    if (!string_equals(&action, "set")) {
        return A1_WIRE_UNSUPPORTED;
    }
    request->action = A1_WIRE_SCHEDULE_SET;
    result = get_uint64_field(json, length, "current",
                              &request->schedule.current_seconds,
                              false, false, NULL);
    if (result != A1_WIRE_OK && result != 1) {
        return result;
    }
    result = get_field(json, length, "params", &params, false);
    if (result == 1) {
        request->params_present = false;
        return A1_WIRE_OK;
    }
    if (result != A1_WIRE_OK) {
        return result;
    }
    request->params_present = true;
    if (params.type != A1_JSON_ARRAY) {
        return A1_WIRE_WRONG_TYPE;
    }
    if (a1_json_array_count(&params, &count) != 0) {
        return A1_WIRE_INVALID_JSON;
    }
    if (count > A1_SCHEDULE_MAX_ENTRIES) {
        count = A1_SCHEDULE_MAX_ENTRIES;
        request->truncated = true;
    }
    request->schedule.count = (uint32_t)count;
    for (index = 0u; index < count; ++index) {
        a1_json_value_t item;
        a1_schedule_entry_t *entry = &request->schedule.entries[index];
        bool sid_present = false;
        if (a1_json_array_get(&params, index, &item) != 0 ||
            item.type != A1_JSON_OBJECT) {
            return A1_WIRE_WRONG_TYPE;
        }
        result = get_uint64_field(item.bytes, item.length, "start",
                                  &entry->start_seconds, true, false, NULL);
        if (result != A1_WIRE_OK) {
            return result;
        }
        result = get_uint64_field(item.bytes, item.length, "end",
                                  &entry->end_seconds, true, false, NULL);
        if (result != A1_WIRE_OK) {
            return result;
        }
        result = get_uint64_field(item.bytes, item.length, "sid", &value,
                                  false, false, &sid_present);
        if (result != A1_WIRE_OK && result != 1) {
            return result;
        }
        if (sid_present) {
            if (value > UINT16_MAX) {
                return A1_WIRE_INVALID_VALUE;
            }
            entry->schedule_id = (uint16_t)value;
        }
    }
    return a1_schedule_validate_set(&request->schedule) == A1_SCHEDULE_OK
        ? A1_WIRE_OK
        : A1_WIRE_INVALID_VALUE;
}

static int gray_optional_int(
    const a1_json_value_t *params,
    const char *key,
    bool *present,
    int *output)
{
    int64_t value;
    int result = get_int64_field(params->bytes, params->length, key,
                                 &value, false, false, present);
    if (result != A1_WIRE_OK && result != 1) {
        return result;
    }
    if (*present) {
        if (value < INT_MIN || value > INT_MAX) {
            return A1_WIRE_INVALID_VALUE;
        }
        *output = (int)value;
    }
    return A1_WIRE_OK;
}

int a1_wire_decode_gray_switch(
    const uint8_t *json,
    size_t length,
    a1_wire_gray_request_t *request)
{
    a1_json_value_t action;
    a1_json_value_t params;
    uint64_t key;
    int result;

    if (request == NULL) {
        return A1_WIRE_INVALID_VALUE;
    }
    memset(request, 0, sizeof(*request));
    result = get_field(json, length, "action", &action, true);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (action.type != A1_JSON_STRING) {
        return A1_WIRE_WRONG_TYPE;
    }
    if (string_equals(&action, "get")) {
        request->action = A1_WIRE_GRAY_GET;
        return A1_WIRE_OK;
    }
    result = get_field(json, length, "params", &params, true);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (params.type != A1_JSON_OBJECT) {
        return A1_WIRE_WRONG_TYPE;
    }
    if (string_equals(&action, "set")) {
        request->action = A1_WIRE_GRAY_SET;
        result = gray_optional_int(&params, "log_record",
                                   &request->update.has_log_record,
                                   &request->update.log_record);
        if (result != A1_WIRE_OK) {
            return result;
        }
        result = gray_optional_int(&params, "stream_record",
                                   &request->update.has_stream_record,
                                   &request->update.stream_record);
        if (result != A1_WIRE_OK) {
            return result;
        }
        result = gray_optional_int(
            &params, "ble_conn_param_auto",
            &request->update.has_ble_connection_parameter_auto,
            &request->update.ble_connection_parameter_auto);
        if (result != A1_WIRE_OK) {
            return result;
        }
        return gray_optional_int(&params, "remark",
                                 &request->update.has_remark,
                                 &request->update.remark);
    }
    if (!string_equals(&action, "control")) {
        return A1_WIRE_UNSUPPORTED;
    }
    request->action = A1_WIRE_GRAY_CONTROL;
    result = get_uint64_field(params.bytes, params.length, "key", &key,
                              true, false, NULL);
    if (result != A1_WIRE_OK) {
        return result;
    }
    if (key > UINT32_MAX) {
        return A1_WIRE_INVALID_VALUE;
    }
    request->control_key = (uint32_t)key;
    result = get_string(params.bytes, params.length, "val",
                        request->control_value, sizeof(request->control_value),
                        false, &request->has_control_value);
    if (result != A1_WIRE_OK && result != 1) {
        return result;
    }
    return A1_WIRE_OK;
}
