#include "a1/wire_responses.h"

#include "a1/json_writer.h"

#define FIELD(writer, key, call) \
    do { \
        if (a1_json_writer_key((writer), (key)) != 0 || (call) != 0) { \
            return (writer)->error; \
        } \
    } while (0)

static int begin_response(
    a1_json_writer_t *writer,
    char *output,
    size_t capacity)
{
    a1_json_writer_init(writer, output, capacity);
    return a1_json_writer_begin_object(writer);
}

static int finish_response(a1_json_writer_t *writer, size_t *written)
{
    if (a1_json_writer_end_object(writer) != 0) {
        return writer->error;
    }
    return a1_json_writer_finish(writer, written);
}

int a1_wire_encode_code(
    char *output,
    size_t capacity,
    uint16_t code,
    size_t *written)
{
    a1_json_writer_t writer;
    if (begin_response(&writer, output, capacity) != 0) {
        return writer.error;
    }
    FIELD(&writer, "code", a1_json_writer_uint64(&writer, code));
    return finish_response(&writer, written);
}

int a1_wire_encode_code_with_type(
    char *output,
    size_t capacity,
    uint16_t code,
    uint32_t type,
    size_t *written)
{
    a1_json_writer_t writer;
    if (begin_response(&writer, output, capacity) != 0) {
        return -1;
    }
    /* Stock dtiot_ble_response_with_type inserts type before code. */
    FIELD(&writer, "type", a1_json_writer_uint64(&writer, type));
    FIELD(&writer, "code", a1_json_writer_uint64(&writer, code));
    return finish_response(&writer, written);
}

int a1_wire_encode_random(
    char *output,
    size_t capacity,
    const char *random_hex,
    size_t *written)
{
    a1_json_writer_t writer;
    if (random_hex == NULL || begin_response(&writer, output, capacity) != 0) {
        return -1;
    }
    FIELD(&writer, "random", a1_json_writer_string(&writer, random_hex));
    return finish_response(&writer, written);
}

int a1_wire_encode_connect(
    char *output,
    size_t capacity,
    uint16_t code,
    const a1_wire_capabilities_t *capabilities,
    size_t *written)
{
    a1_json_writer_t writer;
    if (capabilities == NULL || begin_response(&writer, output, capacity) != 0) {
        return -1;
    }
    FIELD(&writer, "cap_remark",
          a1_json_writer_bool(&writer, capabilities->remark));
    FIELD(&writer, "cap_voiceprint",
          a1_json_writer_bool(&writer, capabilities->voiceprint));
    FIELD(&writer, "cap_incognitomode",
          a1_json_writer_bool(&writer, capabilities->incognito_mode));
    FIELD(&writer, "cap_aikey_option",
          a1_json_writer_bool(&writer, capabilities->ai_key_option));
    FIELD(&writer, "cap_schedule",
          a1_json_writer_bool(&writer, capabilities->schedule));
    FIELD(&writer, "code", a1_json_writer_uint64(&writer, code));
    return finish_response(&writer, written);
}

int a1_wire_encode_device_info(
    char *output,
    size_t capacity,
    uint16_t code,
    const a1_device_identity_t *identity,
    size_t *written)
{
    a1_json_writer_t writer;
    if (identity == NULL || begin_response(&writer, output, capacity) != 0) {
        return -1;
    }
    FIELD(&writer, "pk", a1_json_writer_string(&writer, identity->product_key));
    FIELD(&writer, "dn", a1_json_writer_string(&writer, identity->device_name));
    FIELD(&writer, "supportWifi",
          a1_json_writer_bool(&writer, identity->support_wifi));
    FIELD(&writer, "supportWire",
          a1_json_writer_bool(&writer, identity->support_wire));
    FIELD(&writer, "supportgsm",
          a1_json_writer_bool(&writer, identity->support_gsm));
    FIELD(&writer, "code", a1_json_writer_uint64(&writer, code));
    return finish_response(&writer, written);
}

int a1_wire_encode_audio_status(
    char *output,
    size_t capacity,
    uint16_t code,
    const a1_device_status_t *status,
    const char *version,
    size_t *written)
{
    a1_json_writer_t writer;
    char fid[21];
    size_t index = sizeof(fid) - 1u;
    uint64_t value;

    if (status == NULL || version == NULL ||
        begin_response(&writer, output, capacity) != 0) {
        return -1;
    }
    FIELD(&writer, "code", a1_json_writer_uint64(&writer, code));
    FIELD(&writer, "audio_status",
          a1_json_writer_string(&writer,
              a1_reported_audio_state_name(status->audio_state)));
    FIELD(&writer, "duration", a1_json_writer_uint64(&writer, status->duration_ms));
    if (status->has_fid) {
        fid[index] = '\0';
        value = status->fid;
        do {
            fid[--index] = (char)('0' + value % 10u);
            value /= 10u;
        } while (value != 0u);
        FIELD(&writer, "fid", a1_json_writer_string(&writer, fid + index));
    }
    if (status->has_storage) {
        FIELD(&writer, "storage_total_size",
              a1_json_writer_uint64(&writer, status->storage_total_mib));
        FIELD(&writer, "storage_remain",
              a1_json_writer_uint64(&writer, status->storage_remaining_mib));
    }
    FIELD(&writer, "battery_percent",
          a1_json_writer_uint64(&writer, status->battery_percent));
    FIELD(&writer, "version", a1_json_writer_string(&writer, version));
    return finish_response(&writer, written);
}

static int write_setting(
    a1_json_writer_t *writer,
    const char *key,
    int value)
{
    if (a1_json_writer_begin_object(writer) != 0) {
        return writer->error;
    }
    FIELD(writer, "key", a1_json_writer_string(writer, key));
    FIELD(writer, "val", a1_json_writer_int64(writer, value));
    return a1_json_writer_end_object(writer);
}

int a1_wire_encode_audio_settings(
    char *output,
    size_t capacity,
    uint16_t code,
    const a1_audio_settings_t *settings,
    size_t *written)
{
    a1_json_writer_t writer;
    if (settings == NULL || begin_response(&writer, output, capacity) != 0) {
        return -1;
    }
    FIELD(&writer, "code", a1_json_writer_uint64(&writer, code));
    if (a1_json_writer_key(&writer, "params") != 0 ||
        a1_json_writer_begin_array(&writer) != 0 ||
        write_setting(&writer, "mode", settings->mode) != 0 ||
        write_setting(&writer, "delete_after_upload",
                      settings->delete_after_upload) != 0 ||
        write_setting(&writer, "aes", settings->aes) != 0 ||
        write_setting(&writer, "stream_record", settings->stream_record) != 0 ||
        write_setting(&writer, "incognitomode", settings->incognito_mode) != 0 ||
        write_setting(&writer, "aikey_option", settings->ai_key_option) != 0 ||
        a1_json_writer_end_array(&writer) != 0) {
        return writer.error;
    }
    return finish_response(&writer, written);
}

int a1_wire_encode_wifi(
    char *output,
    size_t capacity,
    uint16_t code,
    const a1_wifi_state_t *wifi,
    size_t *written)
{
    a1_json_writer_t writer;
    if (wifi == NULL || begin_response(&writer, output, capacity) != 0) {
        return -1;
    }
    FIELD(&writer, "ssid", a1_json_writer_string(&writer, wifi->ssid));
    FIELD(&writer, "passwd", a1_json_writer_string(&writer, wifi->password));
    FIELD(&writer, "ip", a1_json_writer_string(&writer, wifi->ip));
    FIELD(&writer, "port", a1_json_writer_uint64(&writer, wifi->port));
    if (wifi->url[0] != '\0') {
        FIELD(&writer, "url", a1_json_writer_string(&writer, wifi->url));
    }
    FIELD(&writer, "code", a1_json_writer_uint64(&writer, code));
    return finish_response(&writer, written);
}

int a1_wire_encode_system_control(
    char *output,
    size_t capacity,
    int32_t key,
    uint16_t code,
    bool include_battery,
    uint8_t battery_percent,
    size_t *written)
{
    a1_json_writer_t writer;
    if (begin_response(&writer, output, capacity) != 0) {
        return writer.error;
    }
    if (include_battery) {
        FIELD(&writer, "battery_percent",
              a1_json_writer_uint64(&writer, battery_percent));
    }
    FIELD(&writer, "key", a1_json_writer_int64(&writer, key));
    FIELD(&writer, "code", a1_json_writer_uint64(&writer, code));
    return finish_response(&writer, written);
}

int a1_wire_encode_gray_switch(
    char *output,
    size_t capacity,
    const a1_gray_switch_state_t *state,
    size_t *written)
{
    a1_json_writer_t writer;
    if (state == NULL || begin_response(&writer, output, capacity) != 0) {
        return -1;
    }
    FIELD(&writer, "log_record",
          a1_json_writer_int64(&writer, state->log_record ? 1 : 0));
    FIELD(&writer, "stream_record",
          a1_json_writer_int64(&writer, state->stream_record ? 1 : 0));
    FIELD(&writer, "ble_conn_param_auto",
          a1_json_writer_int64(&writer,
              state->ble_connection_parameter_auto ? 1 : 0));
    FIELD(&writer, "remark", a1_json_writer_int64(&writer, state->remark));
    return finish_response(&writer, written);
}

int a1_wire_encode_schedule(
    char *output,
    size_t capacity,
    uint16_t code,
    const a1_schedule_t *schedule,
    size_t *written)
{
    a1_json_writer_t writer;
    uint32_t index;
    if (schedule == NULL || schedule->count > A1_SCHEDULE_MAX_ENTRIES ||
        begin_response(&writer, output, capacity) != 0) {
        return -1;
    }
    FIELD(&writer, "code", a1_json_writer_uint64(&writer, code));
    FIELD(&writer, "current",
          a1_json_writer_uint64(&writer, schedule->current_seconds));
    if (a1_json_writer_key(&writer, "params") != 0 ||
        a1_json_writer_begin_array(&writer) != 0) {
        return writer.error;
    }
    for (index = 0u; index < schedule->count; ++index) {
        const a1_schedule_entry_t *entry = &schedule->entries[index];
        if (a1_json_writer_begin_object(&writer) != 0) {
            return writer.error;
        }
        FIELD(&writer, "start",
              a1_json_writer_uint64(&writer, entry->start_seconds));
        FIELD(&writer, "end",
              a1_json_writer_uint64(&writer, entry->end_seconds));
        FIELD(&writer, "sid",
              a1_json_writer_uint64(&writer, entry->schedule_id));
        if (a1_json_writer_end_object(&writer) != 0) {
            return writer.error;
        }
    }
    if (a1_json_writer_end_array(&writer) != 0) {
        return writer.error;
    }
    return finish_response(&writer, written);
}

int a1_wire_encode_firmware_query(
    char *output,
    size_t capacity,
    uint16_t code,
    bool upgrade,
    const char *current_version,
    uint32_t offset,
    size_t *written)
{
    a1_json_writer_t writer;
    if (current_version == NULL ||
        begin_response(&writer, output, capacity) != 0) {
        return -1;
    }
    FIELD(&writer, "code", a1_json_writer_uint64(&writer, code));
    FIELD(&writer, "upgrade", a1_json_writer_bool(&writer, upgrade));
    FIELD(&writer, "cur_ver",
          a1_json_writer_string(&writer, current_version));
    if (upgrade) {
        FIELD(&writer, "offset", a1_json_writer_uint64(&writer, offset));
    }
    return finish_response(&writer, written);
}

#undef FIELD
