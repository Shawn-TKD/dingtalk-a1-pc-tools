#include "a1/audio.h"
#include "a1/audio_container.h"
#include "a1/audio_index.h"
#include "a1/auth.h"
#include "a1/ble_endpoint.h"
#include "a1/ble_link.h"
#include "a1/boot_image.h"
#include "a1/button.h"
#include "a1/companion_core.h"
#include "a1/device_info.h"
#include "a1/display.h"
#include "a1/file_sync.h"
#include "a1/flash_layout.h"
#include "a1/firmware_update.h"
#include "a1/frame.h"
#include "a1/gray_switch.h"
#include "a1/inbound_transfer.h"
#include "a1/json_reader.h"
#include "a1/json_writer.h"
#include "a1/live_audio.h"
#include "a1/memo_container.h"
#include "a1/persistence.h"
#include "a1/protocol_service.h"
#include "a1/raw_transfer.h"
#include "a1/recording_policy.h"
#include "a1/remark.h"
#include "a1/rmt_ipc.h"
#include "a1/router.h"
#include "a1/runtime.h"
#include "a1/schedule.h"
#include "a1/session.h"
#include "a1/system_control.h"
#include "a1/stream_transport.h"
#include "a1/usb_hid.h"
#include "a1/wifi.h"
#include "a1/wifi_tcp.h"
#include "a1/wire_requests.h"
#include "a1/wire_responses.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned responses;
    uint16_t last_command;
    unsigned audio_starts;
    unsigned audio_stops;
    unsigned live_starts;
    unsigned live_stops;
    unsigned memo_starts;
    unsigned memo_stops;
    unsigned vibrations;
    unsigned wifi_creates;
    unsigned wifi_destroys;
    unsigned sync_cancels;
    int performance_locked;
    int wifi_mode;
    char last_json[2048];
    size_t last_json_length;
    unsigned file_lists;
    unsigned file_syncs;
    unsigned file_deletes;
    unsigned raw_starts;
    unsigned raw_cancels;
    unsigned ota_queries;
    unsigned inbound_begins;
    unsigned inbound_writes;
    unsigned inbound_finishes;
    unsigned inbound_cancels;
    uint32_t inbound_expected_hash;
    uint32_t inbound_total_written;
    uint64_t last_fid;
    uint32_t last_offset;
    char last_target_version[A1_UPDATE_VERSION_CAPACITY];
} fake_context_t;

typedef struct {
    struct {
        const char *key;
        int value;
        bool present;
    } integers[16];
    size_t integer_count;
    uint8_t blob[A1_SCHEDULE_FILE_HEADER_SIZE +
                 A1_SCHEDULE_MAX_ENTRIES * A1_SCHEDULE_FILE_ENTRY_SIZE];
    size_t blob_length;
    bool blob_present;
    bool fail_writes;
    unsigned integer_writes;
    unsigned blob_writes;
    unsigned blob_removes;
} fake_persistence_t;

static int fake_persistence_read_int(void *context,
                                     const char *key,
                                     int *value)
{
    fake_persistence_t *fake = context;
    size_t index;
    for (index = 0u; index < fake->integer_count; ++index) {
        if (fake->integers[index].present &&
            strcmp(fake->integers[index].key, key) == 0) {
            *value = fake->integers[index].value;
            return 0;
        }
    }
    return 1;
}

static int fake_persistence_write_int(void *context,
                                      const char *key,
                                      int value)
{
    fake_persistence_t *fake = context;
    size_t index;
    if (fake->fail_writes) {
        return -1;
    }
    for (index = 0u; index < fake->integer_count; ++index) {
        if (strcmp(fake->integers[index].key, key) == 0) {
            fake->integers[index].value = value;
            fake->integers[index].present = true;
            fake->integer_writes++;
            return 0;
        }
    }
    assert(fake->integer_count <
           sizeof(fake->integers) / sizeof(fake->integers[0]));
    fake->integers[fake->integer_count].key = key;
    fake->integers[fake->integer_count].value = value;
    fake->integers[fake->integer_count].present = true;
    fake->integer_count++;
    fake->integer_writes++;
    return 0;
}

static int fake_persistence_read_blob(void *context,
                                      const char *path,
                                      uint8_t *output,
                                      size_t capacity,
                                      size_t *written)
{
    fake_persistence_t *fake = context;
    assert(strcmp(path, A1_SCHEDULE_STORAGE_PATH) == 0);
    if (!fake->blob_present) {
        return 1;
    }
    if (capacity < fake->blob_length) {
        return -1;
    }
    memcpy(output, fake->blob, fake->blob_length);
    *written = fake->blob_length;
    return 0;
}

static int fake_persistence_write_blob(void *context,
                                       const char *path,
                                       const uint8_t *bytes,
                                       size_t length)
{
    fake_persistence_t *fake = context;
    assert(strcmp(path, A1_SCHEDULE_STORAGE_PATH) == 0);
    assert(length <= sizeof(fake->blob));
    if (fake->fail_writes) {
        return -1;
    }
    memcpy(fake->blob, bytes, length);
    fake->blob_length = length;
    fake->blob_present = true;
    fake->blob_writes++;
    return 0;
}

static int fake_persistence_remove_blob(void *context, const char *path)
{
    fake_persistence_t *fake = context;
    assert(strcmp(path, A1_SCHEDULE_STORAGE_PATH) == 0);
    if (fake->fail_writes) {
        return -1;
    }
    fake->blob_present = false;
    fake->blob_length = 0u;
    fake->blob_removes++;
    return 0;
}

static a1_persistence_ops_t make_persistence(fake_persistence_t *fake)
{
    a1_persistence_ops_t ops;
    memset(&ops, 0, sizeof(ops));
    ops.context = fake;
    ops.read_int = fake_persistence_read_int;
    ops.write_int = fake_persistence_write_int;
    ops.read_blob = fake_persistence_read_blob;
    ops.write_blob = fake_persistence_write_blob;
    ops.remove_blob = fake_persistence_remove_blob;
    return ops;
}

static int fake_send_response(void *context,
                              uint16_t command,
                              uint8_t sequence,
                              uint16_t code,
                              const char *json_fields)
{
    fake_context_t *fake = context;
    (void)sequence;
    (void)code;
    (void)json_fields;
    fake->responses++;
    fake->last_command = command;
    return 0;
}

static int fake_audio_start(void *context)
{
    fake_context_t *fake = context;
    fake->audio_starts++;
    return 0;
}

static int fake_send_json_response(void *context,
                                   uint16_t command,
                                   uint8_t message_id,
                                   const char *body,
                                   size_t body_length)
{
    fake_context_t *fake = context;
    (void)message_id;
    assert(body_length < sizeof(fake->last_json));
    memcpy(fake->last_json, body, body_length);
    fake->last_json[body_length] = '\0';
    fake->last_json_length = body_length;
    fake->responses++;
    fake->last_command = command;
    return 0;
}

static int fake_audio_stop(void *context)
{
    fake_context_t *fake = context;
    fake->audio_stops++;
    return 0;
}

static int fake_live_start(void *context)
{
    fake_context_t *fake = context;
    fake->live_starts++;
    return 0;
}

static int fake_live_stop(void *context)
{
    fake_context_t *fake = context;
    fake->live_stops++;
    return 0;
}

static int fake_memo_start(void *context)
{
    fake_context_t *fake = context;
    fake->memo_starts++;
    return 0;
}

static int fake_memo_stop(void *context)
{
    fake_context_t *fake = context;
    fake->memo_stops++;
    return 0;
}

static int fake_vibrate(void *context, uint32_t duration_ms)
{
    fake_context_t *fake = context;
    assert(duration_ms <= 5000u);
    fake->vibrations++;
    return 0;
}

static int fake_random(void *context, uint8_t *output, size_t length)
{
    size_t index;
    (void)context;
    for (index = 0; index < length; ++index) {
        output[index] = (uint8_t)index;
    }
    return 0;
}

/* Production AES belongs to the platform HAL. This deterministic stand-in
 * verifies authentication orchestration and token formatting. */
static int fake_aes(void *context,
                    const uint8_t key[16],
                    const uint8_t iv[16],
                    const uint8_t *input,
                    size_t length,
                    uint8_t *output)
{
    size_t index;
    (void)context;
    (void)iv;
    for (index = 0; index < length; ++index) {
        output[index] = input[index] ^ key[index % 16u];
    }
    return 0;
}

static int fake_wifi_create(void *context, int mode, const char *ssid, const char *password)
{
    fake_context_t *fake = context;
    assert(strncmp(ssid, "DingTalkA1_", 11u) == 0);
    assert(strlen(password) == 8u);
    fake->wifi_creates++;
    fake->wifi_mode = mode;
    return 0;
}

static int fake_wifi_destroy(void *context)
{
    fake_context_t *fake = context;
    fake->wifi_destroys++;
    return 0;
}

static int fake_wifi_ip(void *context, char *output, size_t output_capacity)
{
    (void)context;
    assert(output_capacity >= sizeof("192.168.4.1"));
    memcpy(output, "192.168.4.1", sizeof("192.168.4.1"));
    return 0;
}

static int fake_cancel_sync(void *context)
{
    fake_context_t *fake = context;
    fake->sync_cancels++;
    return 0;
}

static int fake_start_file_list(void *context,
                                uint64_t start_fid,
                                uint64_t end_fid,
                                int32_t recently,
                                uint8_t message_id)
{
    fake_context_t *fake = context;
    assert(start_fid <= end_fid && recently <= 300 && message_id != 0u);
    fake->file_lists++;
    return 0;
}

static int fake_start_file_sync(void *context,
                                uint64_t fid,
                                uint32_t offset,
                                uint32_t progress,
                                uint8_t message_id)
{
    fake_context_t *fake = context;
    assert(progress <= 100u && message_id != 0u);
    fake->file_syncs++;
    fake->last_fid = fid;
    fake->last_offset = offset;
    return 0;
}

static int fake_delete_recording(void *context, uint64_t fid)
{
    fake_context_t *fake = context;
    fake->file_deletes++;
    fake->last_fid = fid;
    return 0;
}

static int fake_start_raw_transfer(void *context,
                                   const char *path,
                                   uint32_t offset,
                                   uint8_t message_id)
{
    fake_context_t *fake = context;
    assert(path[0] == '/' && message_id != 0u);
    fake->raw_starts++;
    fake->last_offset = offset;
    return 0;
}

static int fake_cancel_raw_transfer(void *context)
{
    fake_context_t *fake = context;
    fake->raw_cancels++;
    return 0;
}

static int fake_battery_percent(void *context)
{
    (void)context;
    return 77;
}

static int fake_get_ota_resume_offset(void *context,
                                      const char *target_version,
                                      uint32_t *offset)
{
    fake_context_t *fake = context;
    size_t length = strlen(target_version);
    assert(length < sizeof(fake->last_target_version));
    memcpy(fake->last_target_version, target_version, length + 1u);
    fake->ota_queries++;
    *offset = 8192u;
    return 0;
}

static int fake_begin_inbound_file(void *context,
                                   int type,
                                   uint32_t total_size,
                                   uint32_t resume_offset,
                                   uint32_t expected_verify_hash,
                                   const char *attrs,
                                   const char *version)
{
    fake_context_t *fake = context;
    assert((type == 0 || type == 1) && total_size >= resume_offset);
    assert(attrs != NULL && (type != 0 || version != NULL));
    fake->inbound_begins++;
    fake->inbound_expected_hash = expected_verify_hash;
    fake->inbound_total_written = 0u;
    return 0;
}

static int fake_write_inbound_file(void *context,
                                   const uint8_t *bytes,
                                   size_t length)
{
    fake_context_t *fake = context;
    assert(bytes != NULL && length != 0u && length <= UINT32_MAX);
    fake->inbound_writes++;
    fake->inbound_total_written += (uint32_t)length;
    return 0;
}

static int fake_finish_inbound_file(void *context,
                                    uint32_t *computed_verify_hash)
{
    fake_context_t *fake = context;
    fake->inbound_finishes++;
    *computed_verify_hash = fake->inbound_expected_hash;
    return 0;
}

static int fake_cancel_inbound_file(void *context)
{
    fake_context_t *fake = context;
    fake->inbound_cancels++;
    return 0;
}

static int fake_performance_lock(void *context, int enabled)
{
    fake_context_t *fake = context;
    fake->performance_locked = enabled;
    return 0;
}

static a1_platform_t make_platform(fake_context_t *fake)
{
    a1_platform_t platform;
    memset(&platform, 0, sizeof(platform));
    platform.context = fake;
    platform.send_response = fake_send_response;
    platform.send_json_response = fake_send_json_response;
    platform.start_recording = fake_audio_start;
    platform.stop_recording = fake_audio_stop;
    platform.start_live_stream = fake_live_start;
    platform.stop_live_stream = fake_live_stop;
    platform.start_voice_memo = fake_memo_start;
    platform.stop_voice_memo = fake_memo_stop;
    platform.start_file_list = fake_start_file_list;
    platform.start_file_sync = fake_start_file_sync;
    platform.delete_recording = fake_delete_recording;
    platform.start_raw_transfer = fake_start_raw_transfer;
    platform.cancel_raw_transfer = fake_cancel_raw_transfer;
    platform.battery_percent = fake_battery_percent;
    platform.get_ota_resume_offset = fake_get_ota_resume_offset;
    platform.begin_inbound_file = fake_begin_inbound_file;
    platform.write_inbound_file = fake_write_inbound_file;
    platform.finish_inbound_file = fake_finish_inbound_file;
    platform.cancel_inbound_file = fake_cancel_inbound_file;
    platform.vibrate_ms = fake_vibrate;
    platform.random_bytes = fake_random;
    platform.aes_128_cbc_encrypt = fake_aes;
    platform.wifi_create_ap = fake_wifi_create;
    platform.wifi_destroy_ap = fake_wifi_destroy;
    platform.wifi_get_ip = fake_wifi_ip;
    platform.cancel_file_sync = fake_cancel_sync;
    platform.set_performance_lock = fake_performance_lock;
    return platform;
}

static char hex_digit(uint8_t nibble)
{
    return (char)(nibble < 10u ? ('0' + nibble) : ('a' + nibble - 10u));
}

static void bytes_to_hex(const uint8_t *input, size_t length, char *output)
{
    size_t index;
    for (index = 0; index < length; ++index) {
        output[index * 2u] = hex_digit((uint8_t)(input[index] >> 4));
        output[index * 2u + 1u] = hex_digit((uint8_t)(input[index] & 0x0fu));
    }
    output[length * 2u] = '\0';
}

static void test_frame_codec(void)
{
    const uint8_t body[] = {0xaa, 0xbb, 0xcc};
    const uint8_t expected_header[] = {0x13, 0x01, 0x33, 0x10, 0, 0, 0, 3};
    uint8_t encoded[16];
    a1_frame_view_t decoded;
    size_t consumed = 0;
    size_t written = 0;

    assert(a1_frame_encode_header(encoded, 0x13, 0x0133, 0x10, sizeof(body)) == 0);
    memcpy(encoded + A1_FRAME_HEADER_SIZE, body, sizeof(body));
    assert(memcmp(encoded, expected_header, sizeof(expected_header)) == 0);
    assert(a1_frame_decode(encoded, sizeof(expected_header) + sizeof(body),
                           &decoded, &consumed) == 0);
    assert(consumed == sizeof(expected_header) + sizeof(body));
    assert(decoded.kind == 0x13 && decoded.command == 0x0133 && decoded.message_id == 0x10);
    assert(decoded.body_length == sizeof(body));
    assert(memcmp(decoded.body, body, sizeof(body)) == 0);
    assert(a1_frame_decode(encoded, sizeof(expected_header) + 2u,
                           &decoded, &consumed) == A1_FRAME_NEED_MORE);
    assert(a1_frame_encode_header(encoded, A1_FRAME_REQUEST, 1, 1,
                                  A1_RX_BODY_LIMIT + 1u) == A1_FRAME_BODY_TOO_LARGE);
    memset(encoded, 0, sizeof(encoded));
    assert(a1_frame_encode(encoded, sizeof(encoded), A1_FRAME_RESPONSE,
                           0x0133u, 0x10u, body, sizeof(body),
                           &written) == A1_FRAME_OK);
    assert(written == A1_FRAME_HEADER_SIZE + sizeof(body));
    assert(a1_frame_decode(encoded, written, &decoded, &consumed) == A1_FRAME_OK &&
           decoded.kind == A1_FRAME_RESPONSE && decoded.command == 0x0133u &&
           decoded.message_id == 0x10u &&
           memcmp(decoded.body, body, sizeof(body)) == 0);
    assert(a1_frame_encode(encoded, A1_FRAME_HEADER_SIZE + sizeof(body) - 1u,
                           A1_FRAME_RESPONSE, 1u, 1u, body, sizeof(body),
                           &written) == A1_FRAME_NEED_MORE);
}

static void test_json_reader(void)
{
    static const uint8_t document[] =
        " {\"did\":\"0012345\",\"timestamp\":1700000000,"
        "\"meta\":{\"model\":\"Android\"},\"params\":["
        "{\"key\":\"mode\",\"val\":2},{\"key\":\"aes\",\"val\":1}],"
        "\"escaped\":\"line\\n\\u4e2d\\uD83D\\uDE00\"} ";
    static const uint8_t minimum[] = "{\"n\":-9223372036854775808}";
    static const uint8_t maximum[] = "{\"n\":\"9223372036854775807\"}";
    static const uint8_t overflow[] = "{\"n\":9223372036854775808}";
    static const uint8_t unsigned_maximum[] =
        "{\"n\":\"18446744073709551615\"}";
    static const uint8_t unsigned_overflow[] =
        "{\"n\":18446744073709551616}";
    static const uint8_t malformed_number[] = "{\"n\":01}";
    static const uint8_t trailing[] = "{\"n\":1}x";
    static const uint8_t trailing_comma[] = "{\"n\":1,}";
    a1_json_value_t value;
    a1_json_value_t item;
    int64_t number;
    uint64_t unsigned_number;
    size_t count;
    char text[32];

    assert(a1_json_object_get(document, sizeof(document) - 1u,
                              "did", &value) == 0);
    assert(a1_json_validate_object(document, sizeof(document) - 1u) == 0);
    assert(a1_json_string_copy(&value, text, sizeof(text)) == 0);
    assert(strcmp(text, "0012345") == 0);
    assert(a1_json_int64(&value, &number) == 0 && number == 12345);
    assert(a1_json_object_get(document, sizeof(document) - 1u,
                              "timestamp", &value) == 0);
    assert(a1_json_int64(&value, &number) == 0 && number == 1700000000);
    assert(a1_json_object_get(document, sizeof(document) - 1u,
                              "params", &value) == 0);
    assert(a1_json_array_count(&value, &count) == 0 && count == 2u);
    assert(a1_json_array_get(&value, 1u, &item) == 0 &&
           item.type == A1_JSON_OBJECT);
    assert(a1_json_object_get(item.bytes, item.length, "key", &value) == 0);
    assert(a1_json_string_copy(&value, text, sizeof(text)) == 0 &&
           strcmp(text, "aes") == 0);
    assert(a1_json_array_get(&item, 0u, &value) == -1);
    assert(a1_json_array_get(&(a1_json_value_t){document, 2u, A1_JSON_ARRAY},
                             0u, &value) == -1);
    assert(a1_json_object_get(document, sizeof(document) - 1u,
                              "escaped", &value) == 0);
    assert(a1_json_string_copy(&value, text, sizeof(text)) == 0);
    assert(strcmp(text, "line\n\xe4\xb8\xad\xf0\x9f\x98\x80") == 0);
    assert(a1_json_object_get(document, sizeof(document) - 1u,
                              "missing", &value) == 1);
    assert(a1_json_object_get(minimum, sizeof(minimum) - 1u, "n", &value) == 0);
    assert(a1_json_int64(&value, &number) == 0 && number == INT64_MIN);
    assert(a1_json_object_get(maximum, sizeof(maximum) - 1u, "n", &value) == 0);
    assert(a1_json_int64(&value, &number) == 0 && number == INT64_MAX);
    assert(a1_json_object_get(overflow, sizeof(overflow) - 1u, "n", &value) == 0);
    assert(a1_json_int64(&value, &number) == -2);
    assert(a1_json_object_get(unsigned_maximum, sizeof(unsigned_maximum) - 1u,
                              "n", &value) == 0);
    assert(a1_json_uint64(&value, &unsigned_number) == 0 &&
           unsigned_number == UINT64_MAX);
    assert(a1_json_object_get(unsigned_overflow, sizeof(unsigned_overflow) - 1u,
                              "n", &value) == 0);
    assert(a1_json_uint64(&value, &unsigned_number) == -2);
    assert(a1_json_object_get(malformed_number, sizeof(malformed_number) - 1u,
                              "n", &value) == -1);
    assert(a1_json_object_get(trailing, sizeof(trailing) - 1u,
                              "n", &value) == -1);
    assert(a1_json_object_get(trailing_comma, sizeof(trailing_comma) - 1u,
                              "n", &value) == -1);
}

static void test_json_writer(void)
{
    char output[160];
    char tiny[8];
    size_t written;
    a1_json_writer_t writer;

    a1_json_writer_init(&writer, output, sizeof(output));
    assert(a1_json_writer_begin_object(&writer) == 0);
    assert(a1_json_writer_key(&writer, "code") == 0);
    assert(a1_json_writer_uint64(&writer, 200u) == 0);
    assert(a1_json_writer_key(&writer, "items") == 0);
    assert(a1_json_writer_begin_array(&writer) == 0);
    assert(a1_json_writer_string(&writer, "A\n\"B") == 0);
    assert(a1_json_writer_begin_object(&writer) == 0);
    assert(a1_json_writer_key(&writer, "min") == 0);
    assert(a1_json_writer_int64(&writer, INT64_MIN) == 0);
    assert(a1_json_writer_key(&writer, "ok") == 0);
    assert(a1_json_writer_bool(&writer, true) == 0);
    assert(a1_json_writer_key(&writer, "none") == 0);
    assert(a1_json_writer_null(&writer) == 0);
    assert(a1_json_writer_end_object(&writer) == 0);
    assert(a1_json_writer_end_array(&writer) == 0);
    assert(a1_json_writer_end_object(&writer) == 0);
    assert(a1_json_writer_finish(&writer, &written) == 0);
    assert(written == strlen(output));
    assert(strcmp(output,
        "{\"code\":200,\"items\":[\"A\\n\\\"B\",{\"min\":-9223372036854775808,"
        "\"ok\":true,\"none\":null}]}") == 0);

    a1_json_writer_init(&writer, tiny, sizeof(tiny));
    assert(a1_json_writer_begin_object(&writer) == 0);
    assert(a1_json_writer_key(&writer, "long") == -2);
    assert(writer.error == -2);
}

static void test_wire_responses(void)
{
    char output[1024];
    size_t written;
    a1_wire_capabilities_t caps = {true, true, true, true, true};
    a1_device_identity_t identity;
    a1_device_status_t status;
    a1_wifi_state_t wifi;
    a1_gray_switch_state_t gray;
    a1_schedule_t schedule;

    assert(a1_wire_encode_code(output, sizeof(output), 405u, &written) == 0);
    assert(strcmp(output, "{\"code\":405}") == 0 && written == strlen(output));
    assert(a1_wire_encode_code_with_type(output, sizeof(output), 403u, 1u,
                                         &written) == 0);
    assert(strcmp(output, "{\"type\":1,\"code\":403}") == 0);
    assert(a1_wire_encode_random(
        output, sizeof(output), "00112233445566778899aabbccddeeff", &written) == 0);
    assert(strcmp(output,
        "{\"random\":\"00112233445566778899aabbccddeeff\"}") == 0);
    assert(a1_wire_encode_connect(output, sizeof(output), 200u,
                                  &caps, &written) == 0);
    assert(strcmp(output,
        "{\"cap_remark\":true,\"cap_voiceprint\":true,"
        "\"cap_incognitomode\":true,\"cap_aikey_option\":true,"
        "\"cap_schedule\":true,\"code\":200}") == 0);

    a1_device_identity_init(&identity);
    memcpy(identity.product_key, "pk-a1", sizeof("pk-a1"));
    memcpy(identity.device_name, "dn-a1", sizeof("dn-a1"));
    identity.support_wifi = true;
    assert(a1_wire_encode_device_info(output, sizeof(output), 200u,
                                       &identity, &written) == 0);
    assert(strcmp(output,
        "{\"pk\":\"pk-a1\",\"dn\":\"dn-a1\",\"supportWifi\":true,"
        "\"supportWire\":false,\"supportgsm\":false,\"code\":200}") == 0);

    a1_device_status_init(&status);
    status.audio_state = A1_REPORTED_AUDIO_RECORDING_STREAMING;
    status.duration_ms = 4321u;
    status.has_fid = true;
    status.fid = UINT64_C(1099511627775);
    status.has_storage = true;
    status.storage_total_mib = 59630u;
    status.storage_remaining_mib = 59576u;
    status.battery_percent = 88u;
    assert(a1_wire_encode_audio_status(output, sizeof(output), 200u,
                                        &status, "V1.6.88", &written) == 0);
    assert(strcmp(output,
        "{\"code\":200,\"audio_status\":\"rec_streaming\","
        "\"duration\":4321,\"fid\":\"1099511627775\","
        "\"storage_total_size\":59630,\"storage_remain\":59576,"
        "\"battery_percent\":88,\"version\":\"V1.6.88\"}") == 0);
    {
        a1_audio_settings_t settings;
        a1_audio_settings_init(&settings);
        settings.mode = 4;
        settings.delete_after_upload = 1;
        settings.aes = 1;
        settings.stream_record = 1;
        settings.incognito_mode = 0;
        settings.ai_key_option = 1001;
        assert(a1_wire_encode_audio_settings(output, sizeof(output), 200u,
                                              &settings, &written) == 0);
        assert(strcmp(output,
            "{\"code\":200,\"params\":[{\"key\":\"mode\",\"val\":4},"
            "{\"key\":\"delete_after_upload\",\"val\":1},"
            "{\"key\":\"aes\",\"val\":1},"
            "{\"key\":\"stream_record\",\"val\":1},"
            "{\"key\":\"incognitomode\",\"val\":0},"
            "{\"key\":\"aikey_option\",\"val\":1001}]}") == 0);
    }

    a1_wifi_init(&wifi);
    wifi.opened = 1;
    wifi.mode = A1_WIFI_MODE_HTTP_AUDIO;
    wifi.port = 80u;
    memcpy(wifi.ssid, "DingTalkA1_ABC123", sizeof("DingTalkA1_ABC123"));
    memcpy(wifi.password, "12345678", sizeof("12345678"));
    memcpy(wifi.ip, "192.168.1.1", sizeof("192.168.1.1"));
    memcpy(wifi.url, "http://192.168.1.1/audio/",
           sizeof("http://192.168.1.1/audio/"));
    assert(a1_wire_encode_wifi(output, sizeof(output), 200u,
                                &wifi, &written) == 0);
    assert(strcmp(output,
        "{\"ssid\":\"DingTalkA1_ABC123\",\"passwd\":\"12345678\","
        "\"ip\":\"192.168.1.1\",\"port\":80,"
        "\"url\":\"http://192.168.1.1/audio/\",\"code\":200}") == 0);

    assert(a1_wire_encode_system_control(output, sizeof(output), 10001,
                                          200u, true, 88u, &written) == 0);
    assert(strcmp(output,
        "{\"battery_percent\":88,\"key\":10001,\"code\":200}") == 0);
    a1_gray_switch_init(&gray);
    gray.log_record = true;
    gray.ble_connection_parameter_auto = true;
    gray.remark = 2;
    assert(a1_wire_encode_gray_switch(output, sizeof(output),
                                       &gray, &written) == 0);
    assert(strcmp(output,
        "{\"log_record\":1,\"stream_record\":1,"
        "\"ble_conn_param_auto\":1,\"remark\":2}") == 0);

    a1_schedule_init(&schedule);
    schedule.current_seconds = 1700000000u;
    schedule.count = 1u;
    schedule.entries[0].start_seconds = 1700000100u;
    schedule.entries[0].end_seconds = 1700000200u;
    schedule.entries[0].schedule_id = 7u;
    assert(a1_wire_encode_schedule(output, sizeof(output), 200u,
                                    &schedule, &written) == 0);
    assert(strcmp(output,
        "{\"code\":200,\"current\":1700000000,\"params\":[{"
        "\"start\":1700000100,\"end\":1700000200,\"sid\":7}]}") == 0);
    assert(a1_wire_encode_firmware_query(output, sizeof(output), 200u,
                                          true, "V1.6.88", 8192u,
                                          &written) == 0);
    assert(strcmp(output,
        "{\"code\":200,\"upgrade\":true,\"cur_ver\":\"V1.6.88\","
        "\"offset\":8192}") == 0);
}

static void test_wire_requests(void)
{
    static const uint8_t connect[] =
        "{\"did\":\"93030001\","
        "\"token\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"timestamp\":\"1700000001\",\"model\":\"Android\","
        "\"sdk_ver\":\"1.6.88\"}";
    static const uint8_t short_token[] =
        "{\"did\":\"1\",\"token\":\"abc\"}";
    static const uint8_t audio_set[] =
        "{\"action\":\"set\",\"params\":["
        "{\"key\":\"upload_stream\",\"val\":2},"
        "{\"key\":\"mode\",\"val\":5},"
        "{\"key\":\"aes\",\"val\":1},"
        "{\"key\":\"incognitomode\",\"val\":0},"
        "{\"key\":\"stream_record\",\"val\":1},"
        "{\"key\":\"delete_after_upload\",\"val\":1},"
        "{\"key\":\"force_sync_incognito\",\"val\":0},"
        "{\"key\":\"aikey_option\",\"val\":1001}]}";
    static const uint8_t bad_audio_value[] =
        "{\"action\":\"set\",\"params\":[{\"key\":\"aes\",\"val\":2}]}";
    static const uint8_t unknown_audio_key[] =
        "{\"action\":\"set\",\"params\":[{\"key\":\"new\",\"val\":1}]}";
    static const uint8_t start_audio[] = "{\"action\":\"start\"}";
    static const uint8_t stop_voiceprint[] = "{\"action\":\"stop\"}";
    static const uint8_t open_tcp[] = "{\"type\":1}";
    static const uint8_t open_default[] = "{}";
    static const uint8_t sync[] =
        "{\"fid\":\"1099511627775\",\"offset\":48000,\"progress\":25}";
    static const uint8_t file_list[] =
        "{\"s_fid\":\"1000\",\"e_fid\":\"2000\",\"recently\":30}";
    static const uint8_t file_list_all[] =
        "{\"s_fid\":\"1000\",\"e_fid\":\"2000\"}";
    static const uint8_t fid_only[] = "{\"fid\":\"123456789\"}";
    static const uint8_t remark_json[] =
        "{\"fid\":\"1700000000\",\"ts\":\"1700000002\",\"type\":2}";
    static const uint8_t system_with_value[] = "{\"key\":5,\"val\":1}";
    static const uint8_t system_without_value[] = "{\"key\":10001}";
    static const uint8_t raw[] =
        "{\"path\":\"/emmc/audio/00000000000000\",\"offset\":8000}";
    static const uint8_t schedule_set[] =
        "{\"action\":\"set\",\"current\":1700000000,\"params\":["
        "{\"start\":1700000100,\"end\":1700000200,\"sid\":9},"
        "{\"start\":1700000300,\"end\":1700000400}]}";
    static const uint8_t schedule_clear[] =
        "{\"action\":\"set\",\"current\":1700000000}";
    static const uint8_t schedule_get[] = "{\"action\":\"get\"}";
    static const uint8_t gray_set[] =
        "{\"action\":\"set\",\"params\":{\"log_record\":1,"
        "\"stream_record\":0,\"ble_conn_param_auto\":1,\"remark\":2}}";
    static const uint8_t gray_control[] =
        "{\"action\":\"control\",\"params\":{\"key\":3,"
        "\"val\":\"adbd &\"}}";
    static const uint8_t gray_get[] = "{\"action\":\"get\"}";
    static const uint8_t firmware_query[] =
        "{\"new_ver\":\"V1.6.89-202602010001\"}";
    static const uint8_t firmware_query_missing[] = "{}";
    static const uint8_t firmware_query_wrong_type[] = "{\"new_ver\":7}";
    static const uint8_t inbound_header[] =
        "{\"size\":8196,\"verify_code\":\"0123456789abcdef\","
        "\"attrs\":\"ota@bin\",\"version\":\"V1.6.89-202602010001\"}";
    a1_wire_connect_request_t connect_request;
    a1_wire_audio_request_t audio;
    a1_voiceprint_action_t voiceprint;
    a1_wifi_mode_t wifi_mode;
    a1_wire_file_sync_request_t file_sync;
    a1_wire_file_list_request_t file_list_request;
    a1_remark_t remark;
    a1_wire_system_control_request_t system;
    a1_wire_raw_transfer_request_t raw_request;
    a1_wire_schedule_request_t schedule_request;
    a1_wire_gray_request_t gray_request;
    a1_wire_firmware_query_t firmware;
    a1_wire_file_header_request_t inbound;
    uint64_t fid;

    assert(a1_wire_decode_connect(connect, sizeof(connect) - 1u,
                                  &connect_request) == A1_WIRE_OK);
    assert(connect_request.did == 93030001 &&
           strlen(connect_request.token) == A1_WIRE_TOKEN_LENGTH &&
           connect_request.has_timestamp &&
           connect_request.timestamp == 1700000001 &&
           connect_request.has_model &&
           strcmp(connect_request.model, "Android") == 0 &&
           connect_request.has_sdk_version &&
           strcmp(connect_request.sdk_version, "1.6.88") == 0);
    assert(a1_wire_decode_connect(short_token, sizeof(short_token) - 1u,
                                  &connect_request) == A1_WIRE_INVALID_VALUE);

    assert(a1_wire_decode_audio(audio_set, sizeof(audio_set) - 1u,
                                &audio) == A1_WIRE_OK);
    assert(audio.action == A1_AUDIO_ACTION_SET &&
           audio.settings.present_mask ==
               (A1_AUDIO_SETTING_PRESENT(A1_AUDIO_SETTING_UPLOAD_STREAM) |
                A1_AUDIO_SETTING_PRESENT(A1_AUDIO_SETTING_DELETE_AFTER_UPLOAD) |
                A1_AUDIO_SETTING_PRESENT(A1_AUDIO_SETTING_MODE) |
                A1_AUDIO_SETTING_PRESENT(A1_AUDIO_SETTING_AES) |
                A1_AUDIO_SETTING_PRESENT(A1_AUDIO_SETTING_INCOGNITO_MODE) |
                A1_AUDIO_SETTING_PRESENT(A1_AUDIO_SETTING_FORCE_SYNC_INCOGNITO) |
                A1_AUDIO_SETTING_PRESENT(A1_AUDIO_SETTING_AI_KEY_OPTION) |
                A1_AUDIO_SETTING_PRESENT(A1_AUDIO_SETTING_STREAM_RECORD)) &&
           audio.settings.upload_stream == 2 && audio.settings.mode == 5 &&
           audio.settings.aes == 1 && audio.settings.stream_record == 1 &&
           audio.settings.ai_key_option == 1001);
    assert(a1_wire_decode_audio(start_audio, sizeof(start_audio) - 1u,
                                &audio) == A1_WIRE_OK &&
           audio.action == A1_AUDIO_ACTION_START);
    assert(a1_wire_decode_audio(bad_audio_value, sizeof(bad_audio_value) - 1u,
                                &audio) == A1_WIRE_INVALID_VALUE);
    assert(a1_wire_decode_audio(unknown_audio_key,
                                sizeof(unknown_audio_key) - 1u,
                                &audio) == A1_WIRE_UNSUPPORTED);
    assert(a1_wire_decode_voiceprint(stop_voiceprint,
                                     sizeof(stop_voiceprint) - 1u,
                                     &voiceprint) == A1_WIRE_OK &&
           voiceprint == A1_VOICEPRINT_STOP);

    assert(a1_wire_decode_open_ap(open_tcp, sizeof(open_tcp) - 1u,
                                  &wifi_mode) == A1_WIRE_OK &&
           wifi_mode == A1_WIFI_MODE_TCP_FILE);
    assert(a1_wire_decode_open_ap(open_default, sizeof(open_default) - 1u,
                                  &wifi_mode) == A1_WIRE_OK &&
           wifi_mode == A1_WIFI_MODE_HTTP_AUDIO);
    assert(a1_wire_decode_file_sync(sync, sizeof(sync) - 1u,
                                    &file_sync) == A1_WIRE_OK &&
           file_sync.fid == UINT64_C(1099511627775) &&
           file_sync.offset == 48000u && file_sync.progress == 25u);
    assert(a1_wire_decode_file_list(file_list, sizeof(file_list) - 1u,
                                    &file_list_request) == A1_WIRE_OK &&
           file_list_request.start_fid == 1000u &&
           file_list_request.end_fid == 2000u &&
           file_list_request.recently == 30);
    assert(a1_wire_decode_file_list(file_list_all, sizeof(file_list_all) - 1u,
                                    &file_list_request) == A1_WIRE_OK &&
           file_list_request.recently == -1);
    assert(a1_wire_decode_file_fid(fid_only, sizeof(fid_only) - 1u,
                                   &fid) == A1_WIRE_OK && fid == 123456789u);
    assert(a1_wire_decode_remark(remark_json, sizeof(remark_json) - 1u,
                                 &remark) == A1_WIRE_OK &&
           remark.fid == 1700000000u &&
           remark.timestamp_seconds == 1700000002u && remark.type == 2u);
    assert(a1_wire_decode_system_control(system_with_value,
                                         sizeof(system_with_value) - 1u,
                                         &system) == A1_WIRE_OK &&
           system.key == 5 && system.has_value && system.value == 1);
    assert(a1_wire_decode_system_control(system_without_value,
                                         sizeof(system_without_value) - 1u,
                                         &system) == A1_WIRE_OK &&
           system.key == 10001 && !system.has_value && system.value == -1);
    assert(a1_wire_decode_raw_transfer(raw, sizeof(raw) - 1u,
                                       &raw_request) == A1_WIRE_OK &&
           strcmp(raw_request.path, "/emmc/audio/00000000000000") == 0 &&
           raw_request.offset == 8000u);
    assert(a1_wire_decode_empty_object(open_default,
                                       sizeof(open_default) - 1u) == A1_WIRE_OK);
    assert(a1_wire_decode_schedule(schedule_set, sizeof(schedule_set) - 1u,
                                   &schedule_request) == A1_WIRE_OK &&
           schedule_request.action == A1_WIRE_SCHEDULE_SET &&
           schedule_request.params_present && !schedule_request.truncated &&
           schedule_request.schedule.current_seconds == 1700000000u &&
           schedule_request.schedule.count == 2u &&
           schedule_request.schedule.entries[0].schedule_id == 9u &&
           schedule_request.schedule.entries[1].schedule_id == 0u);
    assert(a1_wire_decode_schedule(schedule_clear, sizeof(schedule_clear) - 1u,
                                   &schedule_request) == A1_WIRE_OK &&
           schedule_request.action == A1_WIRE_SCHEDULE_SET &&
           !schedule_request.params_present &&
           schedule_request.schedule.count == 0u);
    assert(a1_wire_decode_schedule(schedule_get, sizeof(schedule_get) - 1u,
                                   &schedule_request) == A1_WIRE_OK &&
           schedule_request.action == A1_WIRE_SCHEDULE_GET);
    assert(a1_wire_decode_gray_switch(gray_set, sizeof(gray_set) - 1u,
                                      &gray_request) == A1_WIRE_OK &&
           gray_request.action == A1_WIRE_GRAY_SET &&
           gray_request.update.has_log_record &&
           gray_request.update.log_record == 1 &&
           gray_request.update.has_stream_record &&
           gray_request.update.stream_record == 0 &&
           gray_request.update.has_remark &&
           gray_request.update.remark == 2);
    assert(a1_wire_decode_gray_switch(gray_control,
                                      sizeof(gray_control) - 1u,
                                      &gray_request) == A1_WIRE_OK &&
           gray_request.action == A1_WIRE_GRAY_CONTROL &&
           gray_request.control_key == 3u &&
           gray_request.has_control_value &&
           strcmp(gray_request.control_value, "adbd &") == 0);
    assert(a1_wire_decode_gray_switch(gray_get, sizeof(gray_get) - 1u,
                                      &gray_request) == A1_WIRE_OK &&
           gray_request.action == A1_WIRE_GRAY_GET);
    assert(a1_wire_decode_firmware_query(
               firmware_query, sizeof(firmware_query) - 1u,
               &firmware) == A1_WIRE_OK && firmware.has_new_version &&
           strcmp(firmware.new_version, "V1.6.89-202602010001") == 0);
    assert(a1_wire_decode_firmware_query(
               firmware_query_missing, sizeof(firmware_query_missing) - 1u,
               &firmware) == A1_WIRE_OK && !firmware.has_new_version);
    assert(a1_wire_decode_firmware_query(
               firmware_query_wrong_type,
               sizeof(firmware_query_wrong_type) - 1u,
               &firmware) == A1_WIRE_OK && !firmware.has_new_version);
    assert(a1_wire_decode_file_header(
               inbound_header, sizeof(inbound_header) - 1u,
               &inbound) == A1_WIRE_FILE_HEADER_OK &&
           inbound.size == 8196u && inbound.has_version &&
           inbound.verify_hash == a1_djb2_bytes(
               (const uint8_t *)"0123456789abcdef", 16u) &&
           strcmp(inbound.attrs, "ota@bin") == 0);
}

static void test_firmware_version_policy(void)
{
    a1_firmware_version_t version;
    assert(a1_firmware_version_parse("V1.6.88-202601291628", &version) == 0);
    assert(version.major == 1u && version.minor == 6u &&
           version.patch == 88u && version.build == UINT64_C(202601291628));
    assert(a1_firmware_version_is_newer("V1.6.89-202601010000",
                                        "V1.6.88-202601291628"));
    assert(a1_firmware_version_is_newer("V1.6.88-202601291629",
                                        "V1.6.88-202601291628"));
    assert(!a1_firmware_version_is_newer("V1.6.88-202601291628",
                                         "V1.6.88-202601291628"));
    assert(!a1_firmware_version_is_newer("V1.6.87-999999999999",
                                         "V1.6.88-202601291628"));
    assert(a1_firmware_version_parse("1.6.88", &version) != 0);
    assert(a1_firmware_version_parse("V1.6.88-20x", &version) != 0);
}

static void test_inbound_transfer_state(void)
{
    static const uint8_t data[] = {1u, 2u, 3u, 4u};
    uint8_t body[A1_WIFI_TCP_OTA_BLOCK_HEADER_SIZE + sizeof(data)];
    size_t written;
    a1_inbound_block_view_t block;
    a1_inbound_transfer_t transfer;

    assert(a1_wifi_tcp_ota_block_encode(body, sizeof(body), 7u,
                                         data, sizeof(data), &written) == 0);
    assert(a1_inbound_block_decode(body, written, &block) == 0 &&
           block.type == A1_INBOUND_OTA && block.sequence == 7u &&
           block.data_length == sizeof(data));
    a1_inbound_transfer_init(&transfer);
    assert(a1_inbound_transfer_begin(&transfer, A1_INBOUND_OTA,
                                      sizeof(data), 0u, 123u) == 0);
    assert(a1_inbound_transfer_commit_block(&transfer, &block) == 1);
    assert(transfer.complete && !transfer.verified && !transfer.active &&
           transfer.received_size == sizeof(data));
    body[4] ^= 1u;
    assert(a1_inbound_block_decode(body, written, &block) == -4);
    a1_inbound_transfer_cancel(&transfer);
    assert(!transfer.active && !transfer.complete);
}

static void test_flash_layout(void)
{
    static const struct {
        const char *name;
        uint32_t offset;
        uint32_t size;
    } expected[A1_FLASH_PARTITION_COUNT] = {
        {"ota_b", 0x00040000u, 0x000a0000u},
        {"ota", 0x000e0000u, 0x000a0000u},
        {"ota_info", 0x00180000u, 0x00008000u},
        {"ota_flag", 0x00188000u, 0x00008000u},
        {"ap", 0x00190000u, 0x00800000u},
        {"apc1", 0x00990000u, 0x00300000u},
        {"hifi", 0x00c90000u, 0x00170000u},
        {"user", 0x00e00000u, 0x00100000u},
        {"misc", 0x00fe2000u, 0x00001000u},
        {"bootinfo", 0x00fe3000u, 0x00001000u},
        {"bes_reserved", 0x00fe4000u, 0x0001b000u},
        {"factory", 0x00fff000u, 0x00001000u}
    };
    const a1_flash_partition_t *partition;
    size_t index;

    assert(a1_flash_layout_validate() == 0);
    assert(a1_flash_partition_count() == A1_FLASH_PARTITION_COUNT);
    for (index = 0u; index < A1_FLASH_PARTITION_COUNT; ++index) {
        partition = a1_flash_partition_at(index);
        assert(partition != NULL &&
               strcmp(partition->name, expected[index].name) == 0 &&
               partition->offset == expected[index].offset &&
               partition->size == expected[index].size);
    }
    assert(a1_flash_partition_at(A1_FLASH_PARTITION_COUNT) == NULL);
    partition = a1_flash_partition_find("ap");
    assert(partition != NULL &&
           (partition->flags & A1_FLASH_EXECUTABLE) != 0u &&
           a1_flash_partition_contains(partition, 0x00190000u, 1u) &&
           a1_flash_partition_contains(partition, 0x0098ffffu, 1u) &&
           !a1_flash_partition_contains(partition, 0x00990000u, 1u));
    partition = a1_flash_partition_find("factory");
    assert(partition != NULL &&
           (partition->flags & A1_FLASH_IDENTITY_SENSITIVE) != 0u &&
           partition->offset + partition->size == A1_FLASH_TOTAL_SIZE);
    assert(a1_flash_partition_find("missing") == NULL);
}

static void test_boot_image_headers(void)
{
    static const uint8_t ap_ota_header[A1_MCU_IMAGE_HEADER_SIZE] = {
        0xffu, 0xffu, 0xffu, 0xffu,
        0x00u, 0x00u, 0x05u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x44u, 0x62u, 0x54u, 0x30u
    };
    static const uint8_t hifi_header[A1_HIFI_IMAGE_HEADER_MIN_SIZE] = {
        0xffu, 0xffu, 0xffu, 0xffu,
        0x00u, 0x00u, 0x05u, 0x00u,
        0x1cu, 0xecu, 0x57u, 0xbeu,
        0x50u, 0xd1u, 0x0cu, 0x10u,
        0x00u, 0x00u, 0x88u, 0x00u,
        0x00u, 0x00u, 0x90u, 0x00u,
        0x00u, 0x00u, 0x88u, 0x20u,
        0x00u, 0x00u, 0x9cu, 0x20u
    };
    uint8_t installed[A1_MCU_IMAGE_HEADER_SIZE];
    a1_mcu_image_header_t mcu;
    a1_hifi_image_header_t hifi;

    assert(a1_mcu_image_header_decode(ap_ota_header,
                                      sizeof(ap_ota_header), &mcu) == 0);
    assert(mcu.marker == A1_IMAGE_MARKER_OTA_PLACEHOLDER &&
           mcu.format_word == A1_IMAGE_FORMAT_WORD &&
           mcu.build_info_address == 0x30546244u);
    memcpy(installed, ap_ota_header, sizeof(installed));
    installed[0] = 0x1cu;
    installed[1] = 0xecu;
    installed[2] = 0x57u;
    installed[3] = 0xbeu;
    assert(a1_mcu_image_header_decode(installed, sizeof(installed), &mcu) == 0 &&
           mcu.marker == A1_IMAGE_MARKER_INSTALLED);
    installed[0] = 0u;
    assert(a1_mcu_image_header_decode(installed, sizeof(installed), &mcu) == -2);

    assert(a1_hifi_image_header_decode(hifi_header, sizeof(hifi_header),
                                       &hifi) == 0);
    assert(hifi.boot_magic == A1_BOOT_MAGIC &&
           a1_hifi_image_header_matches_v168_map(&hifi));
    hifi.entry_address++;
    assert(!a1_hifi_image_header_matches_v168_map(&hifi));
}

static void test_wifi_tcp_transport(void)
{
    a1_wifi_tcp_decoder_t decoder;
    a1_frame_view_t frame;
    uint8_t first[11] = {0x13, 0x01, 0x14, 0x22, 0, 0, 0, 3, 'o', 't', 'a'};
    uint8_t second[8] = {0x31, 0x01, 0x15, 0x23, 0, 0, 0, 0};
    uint8_t combined[sizeof(first) + sizeof(second)];
    uint8_t invalid[8] = {0x14, 0x01, 0x14, 1, 0, 0, 0, 0};
    uint8_t too_large[8] = {0x13, 0x01, 0x14, 1, 0, 0, 0, 0};

    a1_wifi_tcp_decoder_init(&decoder);
    assert(a1_wifi_tcp_decoder_feed(&decoder, first, 4u) == A1_WIFI_TCP_OK);
    assert(a1_wifi_tcp_decoder_next(&decoder, &frame) == A1_WIFI_TCP_NEED_MORE);
    assert(a1_wifi_tcp_decoder_feed(&decoder, first + 4u, sizeof(first) - 4u) ==
           A1_WIFI_TCP_OK);
    assert(a1_wifi_tcp_decoder_next(&decoder, &frame) == A1_WIFI_TCP_OK);
    assert(frame.kind == A1_FRAME_REQUEST && frame.command == 0x0114u &&
           frame.message_id == 0x22u && frame.body_length == 3u &&
           memcmp(frame.body, "ota", 3u) == 0);
    assert(a1_wifi_tcp_decoder_next(&decoder, &frame) == A1_WIFI_TCP_NEED_MORE);

    memcpy(combined, first, sizeof(first));
    memcpy(combined + sizeof(first), second, sizeof(second));
    a1_wifi_tcp_decoder_reset(&decoder);
    assert(a1_wifi_tcp_decoder_feed(&decoder, combined, sizeof(combined)) ==
           A1_WIFI_TCP_OK);
    assert(a1_wifi_tcp_decoder_next(&decoder, &frame) == A1_WIFI_TCP_OK);
    assert(frame.command == 0x0114u);
    assert(a1_wifi_tcp_decoder_next(&decoder, &frame) == A1_WIFI_TCP_OK);
    assert(frame.kind == A1_FRAME_RESPONSE && frame.command == 0x0115u);
    assert(a1_wifi_tcp_decoder_next(&decoder, &frame) == A1_WIFI_TCP_NEED_MORE);

    a1_wifi_tcp_decoder_reset(&decoder);
    assert(a1_wifi_tcp_decoder_feed(&decoder, invalid, sizeof(invalid)) ==
           A1_WIFI_TCP_OK);
    assert(a1_wifi_tcp_decoder_next(&decoder, &frame) == A1_WIFI_TCP_INVALID_KIND);
    assert(a1_wifi_tcp_decoder_next(&decoder, &frame) == A1_WIFI_TCP_FAULTED);

    too_large[4] = (uint8_t)((A1_WIFI_TCP_MAX_BODY_SIZE + 1u) >> 24);
    too_large[5] = (uint8_t)((A1_WIFI_TCP_MAX_BODY_SIZE + 1u) >> 16);
    too_large[6] = (uint8_t)((A1_WIFI_TCP_MAX_BODY_SIZE + 1u) >> 8);
    too_large[7] = (uint8_t)(A1_WIFI_TCP_MAX_BODY_SIZE + 1u);
    a1_wifi_tcp_decoder_reset(&decoder);
    assert(a1_wifi_tcp_decoder_feed(&decoder, too_large, sizeof(too_large)) ==
           A1_WIFI_TCP_OK);
    assert(a1_wifi_tcp_decoder_next(&decoder, &frame) ==
           A1_WIFI_TCP_PACKET_TOO_LARGE);
    assert(a1_wifi_tcp_command_supported(0x0114u));
    assert(a1_wifi_tcp_command_supported(0x0115u));
    assert(!a1_wifi_tcp_command_supported(0x0116u));
}

static void test_wifi_tcp_ota_block(void)
{
    const uint8_t data[] = "123456789";
    uint8_t encoded[A1_WIFI_TCP_OTA_BLOCK_HEADER_SIZE + sizeof(data) - 1u];
    a1_wifi_tcp_ota_block_view_t block;
    size_t written = 0;

    assert(a1_wifi_tcp_ota_block_encode(encoded, sizeof(encoded), 7u,
                                        data, sizeof(data) - 1u, &written) == 0);
    assert(written == sizeof(encoded));
    assert(memcmp(encoded, "\0\0\0\0\xfc\x89\x19\x18\0\0\0\x07\0\0\0\x09", 16u) == 0);
    assert(a1_wifi_tcp_ota_block_decode(encoded, written, &block) == 0);
    assert(block.transfer_type == 0u && block.sequence == 7u &&
           block.data_length == sizeof(data) - 1u &&
           memcmp(block.data, data, sizeof(data) - 1u) == 0);
    encoded[0] = 1u;
    assert(a1_wifi_tcp_ota_block_decode(encoded, written, &block) == -4);
    encoded[0] = 0u;
    encoded[4] ^= 1u;
    assert(a1_wifi_tcp_ota_block_decode(encoded, written, &block) == -3);
}

static void test_recording_policy(void)
{
    assert(a1_recording_storage_action(0u) == A1_RECORDING_STORAGE_REBOOT);
    assert(a1_recording_storage_action(A1_RECORDING_CLEANUP_WATERMARK_BYTES) ==
           A1_RECORDING_STORAGE_DELETE_OLDEST);
    assert(a1_recording_storage_action(A1_RECORDING_CLEANUP_WATERMARK_BYTES + 1u) ==
           A1_RECORDING_STORAGE_REJECT);
    assert(a1_recording_storage_action(A1_RECORDING_REQUIRED_FREE_BYTES) ==
           A1_RECORDING_STORAGE_REJECT);
    assert(a1_recording_storage_action(A1_RECORDING_REQUIRED_FREE_BYTES + 1u) ==
           A1_RECORDING_STORAGE_ACCEPT);
    assert(a1_recording_flush_chunk_size(0u) == 0u);
    assert(a1_recording_flush_chunk_size(1025u) == 1024u);
    assert(a1_recording_container_data_size(79u) == 0u);
    assert(a1_recording_container_data_size(4880u) == 4800u);
    assert(!a1_recording_should_index_during_flush(4999u));
    assert(a1_recording_should_index_during_flush(5000u));
    assert(a1_recording_rotation_action(2047u, 0) ==
           A1_RECORDING_FINALIZE_DELETE);
    assert(a1_recording_rotation_action(2048u, 0) ==
           A1_RECORDING_FINALIZE_ADD_INDEX);
    assert(a1_recording_rotation_action(2048u, 1) ==
           A1_RECORDING_FINALIZE_UPDATE_INDEX);
    assert(a1_recording_stop_action(1199u, 4800u, 0) ==
           A1_RECORDING_FINALIZE_DELETE);
    assert(a1_recording_stop_action(1200u, 4799u, 0) ==
           A1_RECORDING_FINALIZE_DELETE);
    assert(a1_recording_stop_action(1200u, 4800u, 0) ==
           A1_RECORDING_FINALIZE_ADD_INDEX);
    assert(a1_recording_stop_action(1200u, 4800u, 1) ==
           A1_RECORDING_FINALIZE_UPDATE_INDEX);
}

static void test_ble_link_policy(void)
{
    const uint8_t address[6] = {1u, 2u, 3u, 4u, 5u, 6u};
    a1_ble_link_state_t state;
    a1_ble_link_effect_t effect;
    a1_ble_connection_params_t params;

    assert(a1_ble_phone_model_classify("iPhone15,4", 10u) ==
           A1_BLE_PHONE_IPHONE);
    assert(a1_ble_phone_model_classify("iPad13", 6u) == A1_BLE_PHONE_IPAD);
    assert(a1_ble_phone_model_classify("Android", 7u) ==
           A1_BLE_PHONE_ANDROID);
    assert(a1_ble_phone_model_classify("Harmonyos 4", 11u) ==
           A1_BLE_PHONE_HARMONYOS);
    assert(a1_ble_phone_model_classify("MacBook", 7u) == A1_BLE_PHONE_OTHER);
    assert(a1_ble_phone_model_initial_params(A1_BLE_PHONE_IPHONE, &params) == 1);
    assert(params.interval == 12u && params.latency == 0u &&
           params.supervision_timeout == 400u);
    assert(a1_ble_phone_model_initial_params(A1_BLE_PHONE_ANDROID, &params) == 0);

    a1_ble_link_init(&state);
    assert(a1_ble_link_apply_event(&state, A1_BLE_LINK_CONNECTED, address,
                                   0, 0, &effect) == 0);
    assert(state.physical_connected && memcmp(state.address, address, 6u) == 0);
    assert(effect.stop_advertising && effect.signal_ble_alarm &&
           effect.display_state == 5u);
    state.logically_authenticated = 1;
    assert(a1_ble_link_apply_event(&state, A1_BLE_LINK_DISCONNECTED, address,
                                   1, 0, &effect) == 0);
    assert(!state.physical_connected && !state.logically_authenticated);
    assert(effect.close_wifi_ap && effect.cancel_file_sync &&
           effect.notify_audio_disconnect && effect.start_advertising &&
           effect.clear_peer && effect.display_state == 6u);
    assert(a1_ble_link_apply_event(&state, A1_BLE_LINK_CONNECTED, address,
                                   0, 0, &effect) == 0);
    assert(a1_ble_link_apply_event(&state, A1_BLE_LINK_DISCONNECTED, address,
                                   0, 1, &effect) == 0);
    assert(!effect.start_advertising && !effect.notify_audio_disconnect);
}

static void test_stream_transport(void)
{
    uint8_t first[3] = {1u, 2u, 3u};
    uint8_t second[2] = {4u, 5u};
    uint8_t wire[A1_STREAM_WIRE_HEADER_SIZE + 5u + A1_STREAM_WIRE_TRAILER_SIZE];
    a1_stream_queue_t queue;
    a1_stream_wire_view_t view;
    size_t written = 0u;
    size_t index;

    assert(a1_stream_type_flushes_batch(1u));
    assert(a1_stream_type_flushes_batch(3u));
    assert(a1_stream_type_flushes_batch(5u));
    assert(!a1_stream_type_flushes_batch(0u));
    a1_stream_queue_init(&queue, UINT64_C(0x000000006553f100));
    assert(a1_stream_queue_push(&queue, queue.fid, UINT64_C(1000), 0u,
                                first, sizeof(first)) == 0);
    assert(a1_stream_queue_push(&queue, queue.fid, UINT64_C(1010), 1u,
                                second, sizeof(second)) == 0);
    assert(a1_stream_queue_next_wire(&queue, wire, sizeof(wire), &written) == 0);
    assert(written == sizeof(wire) && queue.count == 0u && queue.sent_bytes == 5u);
    assert(a1_stream_wire_decode(wire, written, &view) == 0);
    assert(view.fid == UINT64_C(0x000000006553f100));
    assert(view.timestamp == UINT64_C(1010) && view.sequence == 2u);
    assert(view.data_length == 5u && view.frame_count == 2u && view.type == 1u);
    assert(memcmp(view.data, "\x01\x02\x03\x04\x05", 5u) == 0);

    a1_stream_queue_init(&queue, 9u);
    for (index = 0u; index < A1_STREAM_QUEUE_LIMIT + 1u; ++index) {
        assert(a1_stream_queue_push(&queue, 9u, index, 0u,
                                    first, sizeof(first)) == 0);
    }
    assert(queue.count == A1_STREAM_QUEUE_LIMIT && queue.dropped_frames == 1u);
    assert(a1_stream_queue_push(&queue, 10u, 0u, 0u, first, sizeof(first)) == -2);
    assert(a1_stream_queue_next_wire(&queue, wire, sizeof(wire), &written) == -2);

    a1_stream_queue_init(&queue, 7u);
    assert(a1_stream_queue_next_wire(&queue, wire, sizeof(wire), &written) == 1);
    assert(written == 0u);
}

static void test_router_registry(void)
{
    fake_context_t fake = {0};
    a1_platform_t platform = make_platform(&fake);
    a1_runtime_t runtime;
    size_t command_count = 0;

    a1_runtime_init(&runtime, &platform);
    a1_command_registry(&command_count);
    assert(command_count == 27u);
    assert(a1_command_find(0x0133) != NULL);
    assert(a1_command_find(0x7fff) == NULL);
    assert(a1_route_request(&runtime, 0x7fff, 1, NULL, 0) == A1_ROUTE_UNSUPPORTED);
    assert(fake.responses == 1u && fake.last_command == 0x7fff);
}

static void test_persistence_and_service_rollback(void)
{
    static const uint8_t secret[A1_DEVICE_SECRET_LENGTH + 1u] =
        "0123456789abcdef0123456789abcdef";
    static const uint8_t audio_set_mode[] =
        "{\"action\":\"set\",\"params\":[{\"key\":\"mode\",\"val\":4}]}";
    static const uint8_t audio_set_mode_failed[] =
        "{\"action\":\"set\",\"params\":[{\"key\":\"mode\",\"val\":5}]}";
    static const uint8_t schedule_set_failed[] =
        "{\"action\":\"set\",\"current\":2000,\"params\":["
        "{\"start\":2100,\"end\":2200,\"sid\":8}]}";
    static const uint8_t gray_set_failed[] =
        "{\"action\":\"set\",\"params\":{\"remark\":0}}";
    fake_persistence_t stored = {0};
    a1_persistence_ops_t persistence = make_persistence(&stored);
    a1_audio_settings_t update;
    a1_audio_settings_t loaded_audio;
    a1_gray_switch_state_t gray;
    a1_gray_switch_state_t loaded_gray;
    a1_gray_switch_update_t gray_update = {0};
    a1_gray_switch_effect_t gray_effect;
    a1_schedule_t schedule;
    a1_schedule_t loaded_schedule;
    fake_context_t fake = {0};
    a1_platform_t platform = make_platform(&fake);
    a1_protocol_service_t service;
    int value;

    a1_audio_settings_init(&update);
    assert(a1_audio_setting_apply(&update,
                                  A1_AUDIO_SETTING_UPLOAD_STREAM, 1) == 0);
    assert(a1_audio_setting_apply(&update, A1_AUDIO_SETTING_MODE, 3) == 0);
    assert(a1_audio_setting_apply(&update, A1_AUDIO_SETTING_AES, 1) == 0);
    assert(a1_persistence_store_audio_update(&persistence, &update) == 0);
    assert(stored.integer_writes == 2u);
    assert(fake_persistence_read_int(&stored, A1_PERSIST_AUDIO_MODE,
                                     &value) == 0 && value == 3);
    assert(fake_persistence_read_int(&stored, A1_PERSIST_AUDIO_AES,
                                     &value) == 0 && value == 1);
    assert(a1_persistence_load_audio(&persistence, &loaded_audio) == 0);
    assert(loaded_audio.mode == 3 && loaded_audio.aes == 1 &&
           loaded_audio.upload_stream == 0);

    a1_gray_switch_init(&gray);
    gray_update.has_log_record = true;
    gray_update.log_record = 1;
    gray_update.has_stream_record = true;
    gray_update.stream_record = 0;
    a1_gray_switch_apply(&gray, &gray_update, &gray_effect);
    assert(a1_persistence_store_gray_update(&persistence, &gray_update,
                                            &gray) == 0);
    assert(fake_persistence_read_int(&stored, A1_PERSIST_REMARK,
                                     &value) == 0 && value == 0);
    assert(a1_persistence_load_gray(&persistence, &loaded_gray) == 0);
    assert(loaded_gray.log_record && !loaded_gray.stream_record &&
           loaded_gray.remark == 0);

    a1_schedule_init(&schedule);
    schedule.current_seconds = 1000u;
    schedule.count = 1u;
    schedule.entries[0].start_seconds = 1100u;
    schedule.entries[0].end_seconds = 1200u;
    schedule.entries[0].schedule_id = 3u;
    assert(a1_persistence_store_schedule(&persistence, &schedule) == 0);
    assert(stored.blob_writes == 1u && stored.blob_present);
    assert(a1_persistence_load_schedule(&persistence, &loaded_schedule) == 0);
    assert(loaded_schedule.count == 1u &&
           loaded_schedule.entries[0].schedule_id == 3u);
    assert(fake_persistence_write_int(&stored, A1_PERSIST_REMARK, 2) == 0);

    a1_protocol_service_init(&service, &platform, secret,
                             A1_DEVICE_SECRET_LENGTH);
    assert(a1_protocol_service_attach_persistence(&service,
                                                   &persistence) == 0);
    assert(service.audio_settings.mode == 3 &&
           service.gray_switch.log_record &&
           service.gray_switch.remark == 2 &&
           service.schedule.entries[0].schedule_id == 3u);
    service.runtime.logical_session_connected = true;
    service.session.has_active_did = true;
    service.session.active_did = 1;

    assert(a1_protocol_service_handle(&service, A1_CMD_AUDIO, 1u,
                                      audio_set_mode,
                                      sizeof(audio_set_mode) - 1u) ==
           A1_ROUTE_OK);
    assert(service.audio_settings.mode == 4);
    assert(fake_persistence_read_int(&stored, A1_PERSIST_AUDIO_MODE,
                                     &value) == 0 && value == 4);

    stored.fail_writes = true;
    assert(a1_protocol_service_handle(&service, A1_CMD_AUDIO, 2u,
                                      audio_set_mode_failed,
                                      sizeof(audio_set_mode_failed) - 1u) ==
           A1_ROUTE_PLATFORM_ERROR);
    assert(service.audio_settings.mode == 4 &&
           strcmp(fake.last_json, "{\"code\":542}") == 0);
    assert(a1_protocol_service_handle(&service, A1_CMD_SCHEDULE_RECORDING, 3u,
                                      schedule_set_failed,
                                      sizeof(schedule_set_failed) - 1u) ==
           A1_ROUTE_PLATFORM_ERROR);
    assert(service.schedule.entries[0].schedule_id == 3u);
    assert(a1_protocol_service_handle(&service, A1_CMD_GRAY_SWITCH, 4u,
                                      gray_set_failed,
                                      sizeof(gray_set_failed) - 1u) ==
           A1_ROUTE_PLATFORM_ERROR);
    assert(service.gray_switch.remark == 2);

    stored.fail_writes = false;
    a1_schedule_init(&schedule);
    assert(a1_persistence_store_schedule(&persistence, &schedule) == 0);
    assert(!stored.blob_present && stored.blob_removes == 1u);
}

static void test_protocol_service_session(void)
{
    static const uint8_t secret[A1_DEVICE_SECRET_LENGTH + 1u] =
        "0123456789abcdef0123456789abcdef";
    static const uint8_t empty[] = "{}";
    static const uint8_t audio_start[] = "{\"action\":\"start\"}";
    static const uint8_t audio_stop[] = "{\"action\":\"stop\"}";
    static const uint8_t audio_set_mode[] =
        "{\"action\":\"set\",\"params\":[{\"key\":\"mode\",\"val\":4}]}";
    static const uint8_t audio_set_aes[] =
        "{\"action\":\"set\",\"params\":[{\"key\":\"aes\",\"val\":1}]}";
    static const uint8_t audio_get[] = "{\"action\":\"get\"}";
    static const uint8_t voice_start[] = "{\"action\":\"start\"}";
    static const uint8_t voice_stop[] = "{\"action\":\"stop\"}";
    static const uint8_t open_ap[] = "{\"type\":0}";
    static const uint8_t schedule_set[] =
        "{\"action\":\"set\",\"current\":1000,\"params\":["
        "{\"start\":1100,\"end\":1200,\"sid\":3}]}";
    static const uint8_t schedule_get[] = "{\"action\":\"get\"}";
    static const uint8_t file_list_json[] =
        "{\"s_fid\":\"1000\",\"e_fid\":\"2000\",\"recently\":30}";
    static const uint8_t file_sync_json[] =
        "{\"fid\":\"1234\",\"offset\":48000,\"progress\":25}";
    static const uint8_t file_delete_json[] = "{\"fid\":\"1234\"}";
    static const uint8_t raw_transfer_json[] =
        "{\"path\":\"/emmc/audio/00000000000000\",\"offset\":8000}";
    static const uint8_t gray_enable_remark[] =
        "{\"action\":\"set\",\"params\":{\"remark\":2}}";
    static const uint8_t remark_json[] =
        "{\"fid\":\"1700000000\",\"ts\":\"1700000002\",\"type\":2}";
    static const uint8_t battery_json[] = "{\"key\":10001}";
    static const uint8_t firmware_query[] =
        "{\"new_ver\":\"V1.6.89-202602010001\"}";
    static const uint8_t inbound_header[] =
        "{\"size\":8196,\"verify_code\":\"0123456789abcdef\","
        "\"attrs\":\"ota@bin\",\"version\":\"V1.6.89-202602010001\"}";
    static const uint8_t inbound_data[] = {9u, 8u, 7u, 6u};
    fake_context_t fake = {0};
    a1_platform_t platform = make_platform(&fake);
    a1_protocol_service_t service;
    uint8_t encrypted[A1_CHALLENGE_LENGTH];
    char token[A1_TOKEN_LENGTH + 1u];
    char connect[256];
    uint8_t inbound_block[A1_WIFI_TCP_OTA_BLOCK_HEADER_SIZE +
                          sizeof(inbound_data)];
    size_t inbound_block_length;
    size_t index;

    a1_protocol_service_init(&service, &platform, secret, A1_DEVICE_SECRET_LENGTH);
    a1_runtime_set_transport(&service.runtime, true);
    memcpy(service.identity.device_name, "SN1234", sizeof("SN1234"));
    memcpy(service.identity.firmware_version, "V1.6.88-202601291628",
           sizeof("V1.6.88-202601291628"));
    service.status.battery_percent = 77u;
    assert(a1_protocol_service_handle(&service, A1_CMD_AUDIO_STATUS, 1u,
                                      empty, sizeof(empty) - 1u) ==
           A1_ROUTE_REJECTED);
    assert(strcmp(fake.last_json, "{\"code\":405}") == 0);

    assert(a1_protocol_service_handle(&service, A1_CMD_AUTH_RANDOM, 2u,
                                      empty, sizeof(empty) - 1u) == A1_ROUTE_OK);
    assert(strcmp(fake.last_json,
                  "{\"random\":\"000102030405060708090a0b0c0d0e0f\"}") == 0);
    for (index = 0u; index < sizeof(encrypted); ++index) {
        static const char challenge[] = "000102030405060708090a0b0c0d0e0f";
        encrypted[index] = (uint8_t)challenge[index] ^ secret[index % 16u];
    }
    bytes_to_hex(encrypted, sizeof(encrypted), token);
    assert(snprintf(connect, sizeof(connect),
                    "{\"did\":\"123456789\",\"token\":\"%s\","
                    "\"model\":\"Android\",\"sdk_ver\":\"1.6.88\"}",
                    token) > 0);
    assert(a1_protocol_service_handle(
        &service, A1_CMD_CONNECT_DEVICE, 3u,
        (const uint8_t *)connect, strlen(connect)) == A1_ROUTE_OK);
    assert(service.runtime.logical_session_connected &&
           strstr(fake.last_json, "\"code\":200") != NULL);

    assert(a1_protocol_service_handle(&service, A1_CMD_AUDIO, 4u,
                                      audio_start, sizeof(audio_start) - 1u) ==
           A1_ROUTE_OK);
    assert(service.runtime.audio_state == A1_AUDIO_RECORDING &&
           fake.audio_starts == 1u);
    assert(a1_protocol_service_handle(&service, A1_CMD_AUDIO, 5u,
                                      audio_stop, sizeof(audio_stop) - 1u) ==
           A1_ROUTE_OK);
    assert(service.runtime.audio_state == A1_AUDIO_IDLE && fake.audio_stops == 1u);
    assert(a1_protocol_service_handle(&service, A1_CMD_AUDIO, 14u,
                                      audio_set_mode,
                                      sizeof(audio_set_mode) - 1u) == A1_ROUTE_OK);
    assert(a1_protocol_service_handle(&service, A1_CMD_AUDIO, 15u,
                                      audio_set_aes,
                                      sizeof(audio_set_aes) - 1u) == A1_ROUTE_OK);
    assert(service.audio_settings.mode == 4 && service.audio_settings.aes == 1 &&
           (service.audio_settings.present_mask &
            A1_AUDIO_SETTING_PRESENT(A1_AUDIO_SETTING_MODE)) != 0u &&
           (service.audio_settings.present_mask &
            A1_AUDIO_SETTING_PRESENT(A1_AUDIO_SETTING_AES)) != 0u);
    assert(a1_protocol_service_handle(&service, A1_CMD_AUDIO, 16u,
                                      audio_get, sizeof(audio_get) - 1u) ==
           A1_ROUTE_OK);
    assert(strstr(fake.last_json, "\"key\":\"mode\",\"val\":4") != NULL &&
           strstr(fake.last_json, "\"key\":\"aes\",\"val\":1") != NULL);

    assert(a1_protocol_service_handle(&service, A1_CMD_VOICEPRINT, 6u,
                                      voice_start, sizeof(voice_start) - 1u) ==
           A1_ROUTE_OK);
    assert(service.runtime.audio_state == A1_AUDIO_LIVE_STREAM &&
           fake.live_starts == 1u);
    assert(a1_protocol_service_handle(&service, A1_CMD_VOICEPRINT, 7u,
                                      voice_stop, sizeof(voice_stop) - 1u) ==
           A1_ROUTE_OK);
    assert(service.runtime.audio_state == A1_AUDIO_IDLE && fake.live_stops == 1u);

    assert(a1_protocol_service_handle(&service, A1_CMD_SCHEDULE_RECORDING, 8u,
                                      schedule_set,
                                      sizeof(schedule_set) - 1u) == A1_ROUTE_OK);
    assert(service.schedule.count == 1u &&
           service.schedule.entries[0].schedule_id == 3u);
    assert(a1_protocol_service_handle(&service, A1_CMD_SCHEDULE_RECORDING, 9u,
                                      schedule_get,
                                      sizeof(schedule_get) - 1u) == A1_ROUTE_OK);
    assert(strstr(fake.last_json, "\"sid\":3") != NULL);

    assert(a1_protocol_service_handle(&service, A1_CMD_FILE_LIST, 20u,
                                      file_list_json,
                                      sizeof(file_list_json) - 1u) ==
           A1_ROUTE_OK);
    assert(fake.file_lists == 1u);
    assert(a1_protocol_service_handle(&service, A1_CMD_FILE_SYNC, 21u,
                                      file_sync_json,
                                      sizeof(file_sync_json) - 1u) ==
           A1_ROUTE_OK);
    assert(fake.file_syncs == 1u && fake.last_fid == 1234u &&
           fake.last_offset == 48000u);
    assert(a1_protocol_service_handle(&service, A1_CMD_FILE_SYNC_CANCEL, 22u,
                                      empty, sizeof(empty) - 1u) == A1_ROUTE_OK);
    assert(fake.sync_cancels == 1u);
    assert(a1_protocol_service_handle(&service, A1_CMD_FILE_DELETE, 23u,
                                      file_delete_json,
                                      sizeof(file_delete_json) - 1u) ==
           A1_ROUTE_OK);
    assert(fake.file_deletes == 1u && fake.last_fid == 1234u);
    assert(a1_protocol_service_handle(&service, A1_CMD_RAW_TRANSFER, 24u,
                                      raw_transfer_json,
                                      sizeof(raw_transfer_json) - 1u) == A1_ROUTE_OK);
    assert(fake.raw_starts == 1u && fake.last_offset == 8000u);
    assert(a1_protocol_service_handle(&service, A1_CMD_RAW_TRANSFER_CANCEL, 25u,
                                      empty, sizeof(empty) - 1u) == A1_ROUTE_OK);
    assert(fake.raw_cancels == 1u);
    assert(a1_protocol_service_handle(&service, A1_CMD_GRAY_SWITCH, 26u,
                                      gray_enable_remark,
                                      sizeof(gray_enable_remark) - 1u) ==
           A1_ROUTE_OK);
    assert(a1_gray_switch_remark_enabled(&service.gray_switch));
    assert(a1_protocol_service_handle(&service, A1_CMD_REMARK, 27u,
                                      remark_json,
                                      sizeof(remark_json) - 1u) == A1_ROUTE_OK);
    assert(strcmp(fake.last_json, "{\"code\":200}") == 0);
    assert(a1_protocol_service_handle(&service, A1_CMD_SYSTEM_CONTROL, 28u,
                                      battery_json,
                                      sizeof(battery_json) - 1u) == A1_ROUTE_OK);
    assert(strstr(fake.last_json, "\"battery_percent\":77") != NULL);
    assert(a1_protocol_service_handle(&service, A1_CMD_FIRMWARE_VERSION, 29u,
                                      firmware_query,
                                      sizeof(firmware_query) - 1u) ==
           A1_ROUTE_OK);
    assert(service.firmware_query_received &&
           strcmp(service.firmware_target_version,
                  "V1.6.89-202602010001") == 0 &&
           fake.ota_queries == 1u &&
           strstr(fake.last_json, "\"upgrade\":true") != NULL &&
           strstr(fake.last_json, "\"offset\":8192") != NULL);
    assert(a1_protocol_service_handle(&service, A1_CMD_FILE_HEADER, 30u,
                                      inbound_header,
                                      sizeof(inbound_header) - 1u) ==
           A1_ROUTE_OK);
    assert(!service.firmware_query_received &&
           service.inbound_transfer.active &&
           service.inbound_transfer.received_size == 8192u &&
           fake.inbound_begins == 1u &&
           strcmp(fake.last_json, "{\"type\":0,\"code\":200}") == 0);
    assert(a1_protocol_service_handle(&service, A1_CMD_FIRMWARE_VERSION, 32u,
                                      firmware_query,
                                      sizeof(firmware_query) - 1u) ==
           A1_ROUTE_OK);
    assert(a1_protocol_service_handle(&service, A1_CMD_FILE_HEADER, 33u,
                                      inbound_header,
                                      sizeof(inbound_header) - 1u) ==
           A1_ROUTE_OK);
    assert(fake.ota_queries == 2u && fake.inbound_begins == 2u &&
           fake.inbound_cancels == 1u &&
           service.inbound_transfer.active &&
           service.inbound_transfer.received_size == 8192u);
    assert(a1_wifi_tcp_ota_block_encode(
               inbound_block, sizeof(inbound_block), 1u,
               inbound_data, sizeof(inbound_data),
               &inbound_block_length) == 0);
    inbound_block[4] ^= 1u;
    assert(a1_protocol_service_handle(&service, A1_CMD_FILE_BLOCK, 31u,
                                      inbound_block,
                                      inbound_block_length) ==
           A1_ROUTE_REJECTED);
    assert(fake.inbound_writes == 0u &&
           service.inbound_transfer.received_size == 8192u &&
           strcmp(fake.last_json, "{\"type\":0,\"code\":403}") == 0);
    inbound_block[4] ^= 1u;
    assert(a1_protocol_service_handle(&service, A1_CMD_FILE_BLOCK, 31u,
                                      inbound_block,
                                      inbound_block_length) == A1_ROUTE_OK);
    assert(service.inbound_transfer.complete &&
           service.inbound_transfer.verified &&
           fake.inbound_writes == 1u && fake.inbound_finishes == 1u &&
           fake.inbound_total_written == sizeof(inbound_data));

    assert(a1_protocol_service_handle(&service, A1_CMD_OPEN_AP, 10u,
                                      open_ap, sizeof(open_ap) - 1u) == A1_ROUTE_OK);
    assert(service.wifi.opened && fake.wifi_creates == 1u &&
           strstr(fake.last_json, "DingTalkA1_") != NULL);
    assert(a1_protocol_service_handle(&service, A1_CMD_CLOSE_AP, 11u,
                                      empty, sizeof(empty) - 1u) == A1_ROUTE_OK);
    assert(!service.wifi.opened && fake.wifi_destroys == 1u);

    assert(a1_protocol_service_handle(&service, A1_CMD_DISCONNECT_DEVICE, 12u,
                                      empty, sizeof(empty) - 1u) == A1_ROUTE_OK);
    assert(!service.runtime.logical_session_connected &&
           !service.session.has_active_did);
    assert(a1_protocol_service_handle(&service, 0x7fffu, 13u,
                                      empty, sizeof(empty) - 1u) ==
           A1_ROUTE_UNSUPPORTED);
    assert(strcmp(fake.last_json, "{\"code\":200}") == 0);
}

static void test_ble_endpoint_stream(void)
{
    static const uint8_t secret[A1_DEVICE_SECRET_LENGTH + 1u] =
        "0123456789abcdef0123456789abcdef";
    static const uint8_t body[] = "{}";
    fake_context_t fake = {0};
    a1_platform_t platform = make_platform(&fake);
    a1_protocol_service_t service;
    a1_ble_endpoint_t endpoint;
    uint8_t stream[2u * (A1_FRAME_HEADER_SIZE + sizeof(body) - 1u)];
    size_t frame_length = A1_FRAME_HEADER_SIZE + sizeof(body) - 1u;
    size_t handled;

    a1_protocol_service_init(&service, &platform, secret, A1_DEVICE_SECRET_LENGTH);
    a1_ble_endpoint_init(&endpoint, &service);
    assert(a1_frame_encode_header(stream, A1_FRAME_REQUEST,
                                  A1_CMD_DEVICE_INFO, 1u,
                                  sizeof(body) - 1u) == A1_FRAME_OK);
    memcpy(stream + A1_FRAME_HEADER_SIZE, body, sizeof(body) - 1u);
    assert(a1_frame_encode_header(stream + frame_length, A1_FRAME_REQUEST,
                                  0x7fffu, 2u,
                                  sizeof(body) - 1u) == A1_FRAME_OK);
    memcpy(stream + frame_length + A1_FRAME_HEADER_SIZE,
           body, sizeof(body) - 1u);

    assert(a1_ble_endpoint_feed(&endpoint, stream, 3u, &handled) ==
           A1_BLE_ENDPOINT_NEED_MORE && handled == 0u);
    assert(a1_ble_endpoint_feed(&endpoint, stream + 3u,
                                sizeof(stream) - 3u, &handled) ==
           A1_BLE_ENDPOINT_OK && handled == 2u);
    assert(endpoint.length == 0u && fake.responses == 2u &&
           fake.last_command == 0x7fffu &&
           strcmp(fake.last_json, "{\"code\":200}") == 0);

    a1_ble_endpoint_reset(&endpoint);
    stream[0] = A1_FRAME_RESPONSE;
    assert(a1_ble_endpoint_feed(&endpoint, stream, frame_length, &handled) ==
           A1_BLE_ENDPOINT_INVALID_KIND);
    assert(a1_ble_endpoint_feed(&endpoint, NULL, 0u, &handled) ==
           A1_BLE_ENDPOINT_FAULTED);
}

static void test_audio_and_ai_key(void)
{
    fake_context_t fake = {0};
    a1_platform_t platform = make_platform(&fake);
    a1_runtime_t runtime;
    a1_audio_settings_t settings;

    a1_runtime_init(&runtime, &platform);
    a1_audio_settings_init(&settings);
    assert(settings.present_mask == 0u);
    assert(a1_audio_setting_apply(&settings, A1_AUDIO_SETTING_UPLOAD_STREAM, 2) == 0);
    assert(a1_audio_setting_apply(&settings, A1_AUDIO_SETTING_UPLOAD_STREAM, 3) != 0);
    assert(a1_audio_apply_action(&runtime, A1_AUDIO_ACTION_START) == 0);
    assert(runtime.audio_state == A1_AUDIO_RECORDING && fake.audio_starts == 1u);
    assert(a1_audio_apply_action(&runtime, A1_AUDIO_ACTION_STOP) == 0);
    assert(runtime.audio_state == A1_AUDIO_IDLE && fake.audio_stops == 1u);

    assert(a1_runtime_ai_key_long_press(&runtime) == 0);
    assert(runtime.audio_state == A1_AUDIO_VOICE_MEMO && fake.memo_starts == 1u);
    assert(a1_runtime_ai_key_release(&runtime) == 0);
    assert(fake.memo_stops == 1u);

    a1_runtime_set_logical_session(&runtime, true);
    assert(a1_runtime_ai_key_long_press(&runtime) == 0);
    assert(runtime.audio_state == A1_AUDIO_LIVE_STREAM && fake.live_starts == 1u);
    assert(a1_runtime_ai_key_release(&runtime) == 0);
    assert(fake.live_stops == 1u);
    assert(a1_runtime_vibrate(&runtime, 5001) != 0);
    assert(a1_runtime_vibrate(&runtime, 250) == 0 && fake.vibrations == 1u);
}

static void test_auth_and_session(void)
{
    static const uint8_t secret[A1_DEVICE_SECRET_LENGTH + 1u] =
        "0123456789abcdef0123456789abcdef";
    static const int64_t did = 1234567890123456789ll;
    fake_context_t fake = {0};
    a1_platform_t platform = make_platform(&fake);
    a1_runtime_t runtime;
    a1_auth_state_t auth;
    a1_session_t session;
    char challenge_hex[A1_CHALLENGE_LENGTH + 1u];
    char token_hex[A1_TOKEN_LENGTH + 1u];
    uint8_t encrypted[A1_CHALLENGE_LENGTH];
    uint8_t hardware_uid[A1_HARDWARE_UID_LENGTH];
    char derived_secret[A1_DEVICE_SECRET_LENGTH + 1u];
    a1_connect_request_t request;
    size_t index;

    for (index = 0; index < sizeof(hardware_uid); ++index) {
        hardware_uid[index] = (uint8_t)index;
    }
    assert(a1_device_secret_derive(hardware_uid, derived_secret) == 0);
    assert(strcmp(derived_secret, "18cacceb8eb59267bcbbbecbf785e2d3") == 0);

    a1_runtime_init(&runtime, &platform);
    a1_auth_init(&auth, secret, A1_DEVICE_SECRET_LENGTH);
    assert(a1_auth_create_challenge(&auth, &platform, challenge_hex) == 0);
    assert(strcmp(challenge_hex, "000102030405060708090a0b0c0d0e0f") == 0);
    for (index = 0; index < sizeof(encrypted); ++index) {
        encrypted[index] = (uint8_t)challenge_hex[index] ^ secret[index % 16u];
    }
    bytes_to_hex(encrypted, sizeof(encrypted), token_hex);
    assert(a1_auth_verify_token(&auth, token_hex, A1_TOKEN_LENGTH) == 0);
    assert(a1_auth_verify_token(&auth, token_hex, A1_TOKEN_LENGTH) != 0);

    a1_session_init(&session, secret, A1_DEVICE_SECRET_LENGTH);
    a1_runtime_set_transport(&runtime, true);
    assert(a1_session_create_challenge(&session, &platform, challenge_hex) == 0);
    memset(&request, 0, sizeof(request));
    request.did = did;
    request.token = token_hex;
    request.token_length = A1_TOKEN_LENGTH;
    assert(a1_session_connect(&session, &runtime, &request) == A1_SESSION_CODE_OK);
    assert(runtime.logical_session_connected);
    assert(a1_session_disconnect(&session, &runtime, -1) == A1_SESSION_CODE_DID_MISMATCH);
    assert(a1_session_disconnect(&session, &runtime, did) == A1_SESSION_CODE_OK);
    assert(runtime.transport_connected && !runtime.logical_session_connected);
}

static void test_button_policy(void)
{
    a1_button_state_t state;
    a1_button_context_t context = {0};
    a1_button_decision_t result;

    a1_button_state_init(&state);
    context.bound = true;
    context.ai_key_option = 1000;
    context.audio_state = A1_AUDIO_IDLE;

    result = a1_button_decide(&state, &context, A1_BUTTON_AI_DOWN, 1000u);
    assert(result.result == A1_BUTTON_HANDLED);
    result = a1_button_decide(&state, &context, A1_BUTTON_AI_LONG_PRESS, 1400u);
    assert(a1_button_decision_has(&result, A1_BUTTON_ACTION_START_VOICE_MEMO));
    context.audio_state = A1_AUDIO_VOICE_MEMO;
    result = a1_button_decide(&state, &context, A1_BUTTON_AI_UP, 1600u);
    assert(a1_button_decision_has(&result, A1_BUTTON_ACTION_STOP_VOICE_MEMO));

    context.audio_state = A1_AUDIO_IDLE;
    context.logical_session_connected = true;
    result = a1_button_decide(&state, &context, A1_BUTTON_AI_LONG_PRESS, 2000u);
    assert(a1_button_decision_has(&result, A1_BUTTON_ACTION_START_LIVE_STREAM));

    result = a1_button_decide(&state, &context, A1_BUTTON_RECORD_DOWN, 3000u);
    assert(result.result == A1_BUTTON_HANDLED);
    result = a1_button_decide(&state, &context, A1_BUTTON_RECORD_UP, 3789u);
    assert(result.press_duration_ms == 789u);
    assert(!a1_button_decision_has(&result, A1_BUTTON_ACTION_START_RECORDING));
    result = a1_button_decide(&state, &context, A1_BUTTON_RECORD_DOWN, 4000u);
    assert(result.result == A1_BUTTON_HANDLED);
    result = a1_button_decide(&state, &context, A1_BUTTON_RECORD_UP, 4790u);
    assert(a1_button_decision_has(&result, A1_BUTTON_ACTION_START_RECORDING));

    context.audio_state = A1_AUDIO_RECORDING;
    context.marker_enabled = true;
    result = a1_button_decide(&state, &context, A1_BUTTON_RECORD_CLICK, 5000u);
    assert(a1_button_decision_has(&result, A1_BUTTON_ACTION_ADD_MARKER));

    context.audio_state = A1_AUDIO_IDLE;
    context.ai_key_option = 1001;
    result = a1_button_decide(&state, &context, A1_BUTTON_AI_DOWN, 6000u);
    assert(result.result == A1_BUTTON_HANDLED);
    result = a1_button_decide(&state, &context, A1_BUTTON_AI_LONG_PRESS, 6390u);
    assert(a1_button_decision_has(&result, A1_BUTTON_ACTION_SHOW_RELEASE_TO_RECORD));
    result = a1_button_decide(&state, &context, A1_BUTTON_AI_UP, 6390u);
    assert(a1_button_decision_has(&result, A1_BUTTON_ACTION_START_RECORDING));

    result = a1_button_decide(&state, &context,
                              A1_BUTTON_RECORD_LONG_LONG_PRESS, 8000u);
    assert(state.shutdown_confirmation_active);
    assert(a1_button_decision_has(
        &result, A1_BUTTON_ACTION_BEGIN_SHUTDOWN_CONFIRMATION));
    result = a1_button_decide(&state, &context, A1_BUTTON_RECORD_UP, 8100u);
    assert(!state.shutdown_confirmation_active);
    assert(a1_button_decision_has(&result, A1_BUTTON_ACTION_CANCEL_SHUTDOWN));

    result = a1_button_decide(&state, &context,
                              A1_BUTTON_RECORD_LONG_LONG_PRESS, 9000u);
    assert(result.result == A1_BUTTON_HANDLED);
    result = a1_button_finish_shutdown_confirmation(&state, A1_AUDIO_RECORDING);
    assert(a1_button_decision_has(&result, A1_BUTTON_ACTION_STOP_RECORDING));
    assert(a1_button_decision_has(&result, A1_BUTTON_ACTION_SHUTDOWN));

    context.ota_upgrade_active = true;
    result = a1_button_decide(&state, &context, A1_BUTTON_AI_CLICK, 10000u);
    assert(result.result == A1_BUTTON_IGNORED && result.action_count == 0u);
}

static void test_display_state_names(void)
{
    assert(strcmp(a1_display_state_name(A1_DISPLAY_RECORDING), "RECORDING") == 0);
    assert(strcmp(a1_display_state_name(A1_DISPLAY_VOICE_MEMO_ANIM),
                  "VOICE_MEMO_ANIM") == 0);
    assert(strcmp(a1_display_state_name(A1_DISPLAY_DEVICE_UPGRADING),
                  "DEVICE_UPGRADING") == 0);
    assert(strcmp(a1_display_state_name(A1_DISPLAY_CHARGING), "CHARGING") == 0);
    assert(a1_display_state_name(43) == NULL);
    assert(!a1_display_state_is_transient(A1_DISPLAY_RECORDING));
    assert(!a1_display_state_is_transient(A1_DISPLAY_VOICE_MEMO_ANIM));
    assert(a1_display_state_is_transient(A1_DISPLAY_SYNC_COMPLETE));
    assert(a1_display_state_is_transient(A1_DISPLAY_ACTIVATION_FAILED));
    assert(a1_display_auto_return_target(
               A1_DISPLAY_ACTIVATION_FAILED,
               A1_DISPLAY_STANDBY_WELCOME,
               false) == A1_DISPLAY_ACTIVATION_REMIND);
    assert(a1_display_auto_return_target(
               A1_DISPLAY_SYNC_COMPLETE,
               A1_DISPLAY_STANDBY_WELCOME,
               true) == A1_DISPLAY_CHARGING);
    assert(a1_display_auto_return_target(
               A1_DISPLAY_RECORDING,
               A1_DISPLAY_STANDBY_WELCOME,
               false) == A1_DISPLAY_RECORDING);
}

static void test_device_info(void)
{
    a1_device_identity_t identity;
    a1_device_status_t status;

    a1_device_identity_init(&identity);
    a1_device_status_init(&status);
    assert(identity.product_key[0] == '\0');
    assert(status.audio_state == A1_REPORTED_AUDIO_IDLE);
    assert(!status.has_fid && !status.has_storage);

    assert(strcmp(a1_reported_audio_state_name(A1_REPORTED_AUDIO_IDLE),
                  "idle") == 0);
    assert(strcmp(a1_reported_audio_state_name(A1_REPORTED_AUDIO_RECORDING),
                  "recording") == 0);
    assert(strcmp(a1_reported_audio_state_name(A1_REPORTED_AUDIO_PAUSED),
                  "paused") == 0);
    assert(strcmp(a1_reported_audio_state_name(A1_REPORTED_AUDIO_STREAMING),
                  "streaming") == 0);
    assert(strcmp(a1_reported_audio_state_name(
                      A1_REPORTED_AUDIO_RECORDING_STREAMING),
                  "rec_streaming") == 0);
    assert(a1_reported_audio_state_name((a1_reported_audio_state_t)5) == NULL);

    assert(a1_device_status_set_storage_blocks(
               &status, 4096u, 15265280u, 15251456u) == 0);
    assert(status.has_storage);
    assert(status.storage_total_mib == 59630u);
    assert(status.storage_remaining_mib == 59576u);
}

static void test_system_control_plan(void)
{
    a1_system_control_plan_t plan;

    plan = a1_system_control_plan(
        A1_SYSTEM_KEY_CLEAR_RECORDINGS, false, 0, 0);
    assert(plan.response_code == 200u);
    assert(plan.immediate_action_count == 1u);
    assert(plan.immediate_actions[0] == A1_SYSTEM_ACTION_CLEAR_RECORDINGS);
    assert(plan.after_response_action_count == 1u);
    assert(plan.after_response_actions[0] == A1_SYSTEM_ACTION_POWER);

    plan = a1_system_control_plan(A1_SYSTEM_KEY_LOG_RECORDING, true, 1, 0);
    assert(plan.immediate_action_count == 2u);
    assert(plan.immediate_actions[0] == A1_SYSTEM_ACTION_ENABLE_LOG_RECORDING);
    assert(plan.immediate_actions[1] == A1_SYSTEM_ACTION_START_LOG_RECORDING);
    plan = a1_system_control_plan(A1_SYSTEM_KEY_LOG_RECORDING, false, 0, 0);
    assert(plan.response_code == 200u && plan.immediate_action_count == 0u);

    plan = a1_system_control_plan(A1_SYSTEM_KEY_BATTERY_PERCENT, false, 0, 104);
    assert(plan.include_battery_percent && plan.battery_percent == 100u);
    plan = a1_system_control_plan(99, false, 0, 0);
    assert(plan.response_code == 408u);
}

static void test_remark_flow(void)
{
    a1_remark_tracker_t tracker;
    a1_remark_t remark = {1786942515ull, 1786942520ull,
                          A1_REMARK_TYPE_APP_FEEDBACK};
    a1_remark_feedback_result_t result;

    a1_remark_tracker_init(&tracker);
    assert(a1_remark_track_button(&tracker, 100u));
    assert(!a1_remark_track_button(&tracker, 102u));
    assert(a1_remark_take_pending(&tracker) == 100u);
    assert(a1_remark_take_pending(&tracker) == 0u);
    assert(a1_remark_track_button(&tracker, 103u));

    result = a1_remark_handle_app_feedback(&remark, true);
    assert(result.response_code == 200u && result.show_display_feedback);
    remark.timestamp_seconds = remark.fid;
    result = a1_remark_handle_app_feedback(&remark, true);
    assert(result.response_code == 500u && result.show_display_feedback);
    result = a1_remark_handle_app_feedback(&remark, false);
    assert(result.response_code == 408u && !result.show_display_feedback);
}

static void test_memo_container(void)
{
    const uint8_t header_bytes[A1_MEMO_CONTAINER_HEADER_SIZE] =
        {0, 0, 0, 13, 0x5a, 16, 32, 80};
    a1_memo_container_header_t header;
    a1_memo_entry_header_t entry = {1786942515ull, 6u, 160u};
    a1_memo_entry_header_t decoded;
    uint8_t bytes[A1_MEMO_ENTRY_HEADER_SIZE];

    assert(a1_memo_container_header_decode(header_bytes, sizeof(header_bytes), &header) == 0);
    assert(header.count == 13u);
    assert(header.sample_rate_khz == 16u && header.bitrate_kbps == 32u);
    assert(header.packet_size == 80u);
    assert(a1_memo_entry_header_encode(bytes, 80, &entry) == 0);
    assert(a1_memo_entry_header_decode(bytes, sizeof(bytes), 80, &decoded) == 0);
    assert(decoded.fid == entry.fid);
    assert(decoded.declared_duration_seconds == entry.declared_duration_seconds);
    assert(decoded.audio_length == entry.audio_length);
}

static void test_raw_transfer(void)
{
    static const uint8_t crc_vector[] = "123456789";
    uint8_t data[32];
    uint8_t encoded[64];
    a1_raw_block_view_t decoded;
    a1_raw_transfer_t transfer;
    a1_raw_next_action_t action;
    uint32_t offset;
    size_t wanted;
    size_t encoded_length;
    unsigned index;

    assert(a1_crc32_bzip2(crc_vector, 9) == 0xfc891918u);
    for (index = 0; index < sizeof(data); ++index) {
        data[index] = (uint8_t)(index * 3u);
    }
    assert(a1_raw_block_encode(encoded, sizeof(encoded), 7, data, sizeof(data),
                               &encoded_length) == 0);
    assert(a1_raw_block_decode(encoded, encoded_length, &decoded) == 0);
    assert(decoded.sequence == 7u && decoded.data_length == sizeof(data));
    assert(memcmp(decoded.data, data, sizeof(data)) == 0);
    encoded[10] ^= 1u;
    assert(a1_raw_block_decode(encoded, encoded_length, &decoded) == -3);

    memset(&transfer, 0, sizeof(transfer));
    assert(a1_raw_transfer_begin(&transfer, 16001u, 0u) == 0);
    wanted = a1_raw_transfer_next_read(&transfer, &offset);
    assert(offset == 0u && wanted == A1_RAW_BLOCK_DATA_LIMIT);
    assert(a1_raw_transfer_mark_sent(&transfer, wanted, &offset) == 0);
    assert(offset == 1u);
    for (index = 0; index < 3u; ++index) {
        action = a1_raw_transfer_on_response(&transfer, 0x0193);
        assert(action == A1_RAW_NEXT_RESEND);
        wanted = a1_raw_transfer_next_read(&transfer, &offset);
        assert(offset == 0u && wanted == A1_RAW_BLOCK_DATA_LIMIT);
        assert(a1_raw_transfer_mark_sent(&transfer, wanted, &offset) == 0);
        assert(offset == 1u);
    }
    action = a1_raw_transfer_on_response(&transfer, 0x0193);
    assert(action == A1_RAW_NEXT_ABORT && transfer.state == A1_RAW_TRANSFER_ERROR);
}

static void test_file_sync(void)
{
    const uint8_t index_payload[] = {
        0x00, 0xc8, 0x00, 0x02,
        0x00, 0x01, 0x65, 0x53, 0xf1, 0x00, 0x00, 0x2a,
        0x00, 0x02, 0x65, 0x53, 0xf1, 0x01, 0xff, 0xff,
        0x5a, 0x00
    };
    uint8_t block_payload[A1_FILE_BLOCK_HEADER_SIZE + 4u] = {
        0, 0, 0x65, 0x53, 0xf1, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 4,
        'D', 'T', 'Y', 'J'
    };
    a1_file_index_entry_t entries[2];
    a1_file_index_metadata_t metadata;
    a1_file_block_view_t block;
    a1_file_receive_t receive;
    a1_file_send_t sender;
    uint8_t encoded_index[32];
    uint8_t encoded_block[64];
    size_t encoded_length;
    size_t wanted;
    uint32_t offset;
    uint32_t sequence;
    unsigned retry;
    size_t count = 0;

    assert(a1_file_index_decode(index_payload, sizeof(index_payload),
                                entries, 2, &count) == 0);
    assert(count == 2u);
    assert(entries[0].flag == 1u && entries[0].fid == 0x6553f100u);
    assert(entries[0].status_or_duration_raw == 42u);
    assert(entries[1].fid == 0x6553f101u &&
           entries[1].status_or_duration_raw == 0xffffu);
    assert(a1_file_index_decode(index_payload, sizeof(index_payload),
                                entries, 1, &count) == -4);

    {
        const uint8_t stock_truncated_page[] = {
            0x00, 0xc8, 0x00, 0x01,
            0x00, 0x01, 0x65, 0x53, 0xf1, 0x00, 0x00, 0x2a,
            0x00, 0x00, 0x00, 0x01, 'Z', 'Z', 'Z', 'Z'
        };
        assert(a1_file_index_decode_full(
                   stock_truncated_page,
                   sizeof(stock_truncated_page),
                   entries,
                   2,
                   &count,
                   &metadata) == 0);
        assert(count == 1u && metadata.present && metadata.truncated);
    }

    assert(a1_file_list_policy(1, 0, 0, 1, 1, 1, 0xaa55u) ==
           A1_FILE_LIST_PERMITTED);
    assert(a1_file_list_policy(0, 1, 1, 1, 1, 1, 0xaa55u) ==
           A1_FILE_LIST_PERMITTED_FORCE_SYNC);
    assert(a1_file_list_policy(0, 1, 0, 1, 1, 1, 0xaa55u) ==
           A1_FILE_LIST_DENIED_INCOGNITO);
    assert(a1_file_list_policy(0, 0, 0, 1, 0, 1, 0u) ==
           A1_FILE_LIST_DENIED_NEVER_CONFIGURED);
    assert(a1_file_list_policy(0, 0, 0, 0, 0, 0, 0u) ==
           A1_FILE_LIST_DENIED_LEGACY_PEER);

    assert(a1_file_index_encode(encoded_index, sizeof(encoded_index), 200u,
                                entries, 1u, 1, &encoded_length) == 0);
    assert(encoded_length == 20u);
    assert(a1_file_index_decode_full(encoded_index, encoded_length, entries, 2u,
                                     &count, &metadata) == 0);
    assert(metadata.present && metadata.truncated);

    assert(a1_file_block_decode(block_payload, sizeof(block_payload), &block) == 0);
    assert(block.fid == 0x6553f100u && block.number == 0u && block.data_length == 4u);
    assert(memcmp(block.data, "DTYJ", 4) == 0);
    a1_file_receive_begin(&receive, block.fid, 8u, 1);
    assert(a1_file_receive_accept(&receive, &block) == A1_FILE_RECEIVE_WRITE);
    assert(a1_file_receive_accept(&receive, &block) == A1_FILE_RECEIVE_DUPLICATE);
    block_payload[16] = 'B';
    assert(a1_file_block_decode(block_payload, sizeof(block_payload), &block) == 0);
    assert(a1_file_receive_accept(&receive, &block) == A1_FILE_RECEIVE_CHANGED_RETRY);
    block_payload[16] = 'D';
    block_payload[11] = 1;
    assert(a1_file_block_decode(block_payload, sizeof(block_payload), &block) == 0);
    assert(a1_file_receive_accept(&receive, &block) == A1_FILE_RECEIVE_COMPLETE);
    assert(receive.received_size == 8u && receive.complete);

    assert(a1_file_block_encode(encoded_block, sizeof(encoded_block), 0u,
                                0x6553f100u, 0u, 7u,
                                (const uint8_t *)"DTYJ", 4u,
                                &encoded_length) == 0);
    assert(encoded_length == A1_FILE_BLOCK_HEADER_SIZE + 4u +
           A1_FILE_BLOCK_TRAILER_SIZE);
    assert(a1_file_block_decode(encoded_block, encoded_length, &block) == 0);
    assert(block.trailer_present && block.trailing_reserved == 0u &&
           block.crc == a1_crc32_bzip2((const uint8_t *)"DTYJ", 4u));
    encoded_block[encoded_length - 1u] ^= 1u;
    assert(a1_file_block_decode(encoded_block, encoded_length, &block) == -4);

    assert(a1_file_send_begin(&sender, 48001u, 0u) == 0);
    wanted = a1_file_send_next_read(&sender, &offset);
    assert(offset == 0u && wanted == A1_FILE_SEND_BLOCK_DATA_LIMIT);
    assert(a1_file_send_mark_sent(&sender, wanted, &sequence) == 0 && sequence == 1u);
    for (retry = 0; retry < 3u; ++retry) {
        assert(a1_file_send_on_response(&sender, 500u) == A1_FILE_SEND_NEXT_RESEND);
        wanted = a1_file_send_next_read(&sender, &offset);
        assert(offset == 0u && wanted == A1_FILE_SEND_BLOCK_DATA_LIMIT);
        assert(a1_file_send_mark_sent(&sender, wanted, &sequence) == 0 && sequence == 1u);
    }
    assert(a1_file_send_on_response(&sender, 500u) == A1_FILE_SEND_NEXT_ABORT);
    assert(sender.state == A1_FILE_SEND_ERROR);
}

static void test_usb_hid(void)
{
    uint8_t frame[140];
    uint8_t first_report[A1_USB_HID_REPORT_SIZE];
    uint8_t second_report[A1_USB_HID_REPORT_SIZE];
    a1_usb_hid_decoder_t decoder;
    a1_frame_view_t decoded;
    size_t consumed = 0;
    size_t index;

    assert(a1_usb_frame_encode_header(frame, A1_FRAME_REQUEST,
                                      A1_USB_COMMAND_INFO, 11, 132u) == A1_USB_OK);
    assert(memcmp(frame, "\x13\x90\x01\x0b\x84\x00\x00\x00", 8) == 0);
    for (index = A1_FRAME_HEADER_SIZE; index < sizeof(frame); ++index) {
        frame[index] = (uint8_t)index;
    }
    assert(a1_usb_hid_report_count(sizeof(frame)) == 2u);
    assert(a1_usb_hid_make_report(frame, sizeof(frame), 0, first_report) == A1_USB_OK);
    assert(a1_usb_hid_make_report(frame, sizeof(frame), 1, second_report) == A1_USB_OK);
    assert(first_report[0] == A1_USB_HID_REPORT_ID && second_report[0] == A1_USB_HID_REPORT_ID);

    a1_usb_hid_decoder_init(&decoder);
    assert(a1_usb_hid_decoder_feed(&decoder, first_report, sizeof(first_report),
                                   &decoded) == A1_USB_NEED_MORE);
    assert(a1_usb_hid_decoder_feed(&decoder, second_report, sizeof(second_report),
                                   &decoded) == A1_USB_OK);
    assert(decoded.kind == A1_FRAME_REQUEST && decoded.command == A1_USB_COMMAND_INFO);
    assert(decoded.message_id == 11u && decoded.body_length == 132u);
    assert(memcmp(decoded.body, frame + A1_FRAME_HEADER_SIZE, 132u) == 0);
    assert(a1_usb_frame_decode(frame, sizeof(frame), &decoded, &consumed) == A1_USB_OK);
    assert(consumed == sizeof(frame));
    assert(a1_usb_hid_decoder_feed(&decoder, first_report, sizeof(first_report),
                                   &decoded) == A1_USB_DECODER_BUSY);
    a1_usb_hid_decoder_consume(&decoder);
    first_report[0] = 2;
    assert(a1_usb_hid_decoder_feed(&decoder, first_report, sizeof(first_report),
                                   &decoded) == A1_USB_WRONG_REPORT_ID);

    first_report[0] = A1_USB_HID_REPORT_ID;
    a1_usb_hid_decoder_init(&decoder);
    assert(a1_usb_hid_decoder_feed_at(&decoder, first_report, sizeof(first_report),
                                      UINT32_MAX - 1000u,
                                      &decoded) == A1_USB_NEED_MORE);
    assert(a1_usb_hid_decoder_feed_at(&decoder, second_report, sizeof(second_report),
                                      3999u,
                                      &decoded) == A1_USB_OK);
    a1_usb_hid_decoder_consume(&decoder);
    assert(a1_usb_hid_decoder_feed_at(&decoder, first_report, sizeof(first_report),
                                      100u,
                                      &decoded) == A1_USB_NEED_MORE);
    assert(a1_usb_hid_decoder_feed_at(&decoder, second_report, sizeof(second_report),
                                      5101u,
                                      &decoded) == A1_USB_INVALID_FRAME);
}

static void test_audio_index(void)
{
    const uint8_t expected[] = {0x05, 0x01, 0x23, 0x45, 0x67, 0x89, 0x12, 0x34};
    uint8_t packed[A1_AUDIO_INDEX_RECORD_SIZE * 3u];
    uint8_t header_bytes[A1_AUDIO_INDEX_HEADER_SIZE];
    uint64_t output[3];
    a1_audio_index_record_t record = {
        UINT64_C(0x0123456789), 0x1234u, 0x05u
    };
    a1_audio_index_record_t decoded;
    a1_audio_index_header_t header = {42u, {0xde, 0xad, 0xbe, 0xef}};
    a1_audio_index_header_t decoded_header;
    size_t index;

    assert(a1_audio_index_record_encode(&record, packed) == 0);
    assert(memcmp(packed, expected, sizeof(expected)) == 0);
    assert(a1_audio_index_record_decode(packed, &decoded) == 0);
    assert(decoded.fid == record.fid && decoded.duration_seconds == 0x1234u &&
           decoded.flags == 0x05u);
    a1_audio_index_record_mark_deleted(packed);
    assert(a1_audio_index_record_is_deleted(packed));
    assert(a1_audio_index_record_decode(packed, &decoded) == 0 &&
           decoded.flags == 0x85u);

    assert(a1_audio_index_header_encode(&header, header_bytes) == 0);
    assert(memcmp(header_bytes, "\x00\x00\x00\x2a\xde\xad\xbe\xef", 8u) == 0);
    assert(a1_audio_index_header_decode(header_bytes, &decoded_header) == 0);
    assert(decoded_header.live_count == 42u &&
           memcmp(decoded_header.opaque_format, header.opaque_format, 4u) == 0);

    for (index = 0u; index < 3u; ++index) {
        record.fid = 100u + index;
        record.duration_seconds = 1u;
        record.flags = 0u;
        assert(a1_audio_index_record_encode(
                   &record, packed + index * A1_AUDIO_INDEX_RECORD_SIZE) == 0);
    }
    a1_audio_index_record_mark_deleted(packed + A1_AUDIO_INDEX_RECORD_SIZE);
    assert(a1_audio_index_collect_newest(packed, 3u, 100u, 102u,
                                         output, 3u) == 2u);
    assert(output[0] == 102u && output[1] == 100u);
    assert(a1_audio_index_segment_for_ordinal(1999u) == 1u);
    assert(a1_audio_index_segment_offset(2u) == 16008u);
    assert(!a1_audio_index_should_compact(10000u, 1u));
    assert(!a1_audio_index_should_compact(10001u, 5001u));
    assert(a1_audio_index_should_compact(10003u, 5001u));
}

static void test_live_audio_and_ut(void)
{
    uint8_t live[A1_LIVE_AUDIO_HEADER_SIZE + A1_LIVE_OPUS_UNIT_SIZE * 2u] = {0};
    uint8_t ut[A1_UT_ENVELOPE_OVERHEAD + A1_UT_RECORD_SIZE] = {0};
    a1_live_audio_view_t audio;
    a1_ut_record_t record;
    size_t count = 0;

    live[4] = 0x65;
    live[5] = 0x53;
    live[6] = 0xf1;
    live[7] = 0x00;
    live[19] = 7;
    live[23] = 168;
    live[28] = 0x78;
    live[28 + A1_LIVE_OPUS_UNIT_SIZE] = 0x79;
    assert(a1_live_audio_decode(live, sizeof(live), &audio) == 0);
    assert(audio.fid == 0x6553f100u && audio.sequence == 7u);
    assert(audio.opus_unit_count == 2u);
    assert(a1_live_audio_opus_unit(&audio, 0)[0] == 0x78u);
    assert(a1_live_audio_opus_unit(&audio, 1)[0] == 0x79u);
    live[23] = 167;
    assert(a1_live_audio_decode(live, sizeof(live), &audio) == -3);

    ut[0] = 'Z';
    ut[1] = 'Z';
    ut[3] = A1_UT_RECORD_SIZE;
    ut[4] = 0;
    ut[5] = 9;
    ut[8] = 0x65;
    ut[9] = 0x53;
    ut[10] = 0xf1;
    ut[11] = 0x00;
    ut[12] = 0x0b;
    ut[15] = 1;
    ut[24] = 0;
    ut[25] = 0;
    ut[26] = 0;
    ut[27] = 1;
    ut[28] = 'Z';
    ut[29] = 'Z';
    assert(a1_ut_envelope_count(ut, sizeof(ut), &count) == 0 && count == 1u);
    assert(a1_ut_record_decode(ut, sizeof(ut), 0, &record) == 0);
    assert(record.sequence == 9u && record.device_timestamp == 0x6553f100u);
    assert(a1_ut_record_is_marker(&record));
    record.argument0 = 1;
    assert(!a1_ut_record_is_marker(&record));
}

static void write_le16_test(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_le32_test(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void test_audio_container(void)
{
    uint8_t dtyj[68] = {0};
    uint8_t page[128];
    uint8_t page_for_crc[128];
    const uint8_t *packet;
    a1_dtyj_view_t recording;
    size_t packet_length;
    size_t written;
    uint32_t prefix;
    uint32_t samples;
    uint32_t stored_crc;

    memcpy(dtyj, "BABA", 4u);
    write_le32_test(dtyj + 4u, sizeof(dtyj) - 8u);
    memcpy(dtyj + 8u, "DTYJ", 4u);
    memcpy(dtyj + 12u, "fmt ", 4u);
    write_le32_test(dtyj + 24u, 32000u);
    write_le16_test(dtyj + 36u, 8u);
    memcpy(dtyj + 40u, "data", 4u);
    write_le32_test(dtyj + 44u, 16u);
    write_le32_test(dtyj + 52u, 0x000001a0u);
    dtyj[56] = 0u;
    write_le32_test(dtyj + 60u, 0x00000020u);
    dtyj[64] = 8u;

    assert(a1_dtyj_decode(dtyj, sizeof(dtyj), &recording) == 0);
    assert(recording.sample_rate == 32000u && recording.record_size == 8u);
    assert(recording.record_count == 2u);
    packet = a1_dtyj_packet(&recording, 0, &packet_length, &prefix);
    assert(packet != NULL && packet_length == 4u && prefix == 0x000001a0u);
    assert(a1_opus_packet_samples_48k(packet, packet_length, &samples) == 0);
    assert(samples == 480u);
    packet = a1_dtyj_packet(&recording, 1, &packet_length, &prefix);
    assert(prefix == 0x20u);
    assert(a1_opus_packet_samples_48k(packet, packet_length, &samples) == 0);
    assert(samples == 960u);

    assert(a1_ogg_page_encode(page, sizeof(page), packet, packet_length,
                              4u, samples, 0xa1d1a1d1u, 2u, &written) == 0);
    assert(written == a1_ogg_page_size(packet_length));
    assert(memcmp(page, "OggS", 4u) == 0 && page[5] == 4u);
    stored_crc = (uint32_t)page[22] |
                 ((uint32_t)page[23] << 8) |
                 ((uint32_t)page[24] << 16) |
                 ((uint32_t)page[25] << 24);
    memcpy(page_for_crc, page, written);
    memset(page_for_crc + 22u, 0, 4u);
    assert(stored_crc == a1_ogg_crc(page_for_crc, written));
}

static void test_wifi_state(void)
{
    fake_context_t fake = {0};
    a1_platform_t platform = make_platform(&fake);
    a1_wifi_state_t wifi;

    a1_wifi_init(&wifi);
    assert(a1_wifi_open(&wifi, &platform, A1_WIFI_MODE_HTTP_AUDIO,
                        "A1-SERIAL-1234") == 0);
    assert(wifi.opened && wifi.port == 80u && fake.wifi_mode == 0);
    assert(strlen(wifi.ssid) == 17u && strcmp(wifi.ssid + 13u, "1234") == 0);
    assert(strcmp(wifi.ip, "192.168.4.1") == 0);
    assert(strcmp(wifi.url, "http://192.168.4.1/audio/") == 0);
    assert(fake.sync_cancels == 1u && fake.performance_locked == 1);
    assert(a1_wifi_open(&wifi, &platform, A1_WIFI_MODE_TCP_FILE, NULL) == -2);
    assert(a1_wifi_close(&wifi, &platform) == 0);
    assert(!wifi.opened && fake.wifi_destroys == 1u && fake.performance_locked == 0);

    assert(a1_wifi_open(&wifi, &platform, A1_WIFI_MODE_TCP_FILE, NULL) == 0);
    assert(wifi.port == 5922u && wifi.url[0] == '\0' && fake.wifi_mode == 1);
    assert(a1_wifi_close(&wifi, &platform) == 0);
}

static void test_schedule_storage_and_selection(void)
{
    a1_schedule_t schedule;
    a1_schedule_t decoded;
    uint8_t encoded[A1_SCHEDULE_FILE_HEADER_SIZE +
                    2u * A1_SCHEDULE_FILE_ENTRY_SIZE];
    size_t written = 0;
    size_t consumed = 0;
    uint64_t next = 0;

    a1_schedule_init(&schedule);
    schedule.current_seconds = UINT64_C(1700000000);
    schedule.count = 2;
    schedule.entries[0].start_seconds = UINT64_C(1700000100);
    schedule.entries[0].end_seconds = UINT64_C(1700000200);
    schedule.entries[0].schedule_id = 7;
    schedule.entries[1].start_seconds = UINT64_C(1700000300);
    schedule.entries[1].end_seconds = UINT64_C(1700000400);
    schedule.entries[1].schedule_id = 9;
    schedule.entries[1].reserved[5] = 0xa5;

    assert(a1_schedule_validate_set(&schedule) == A1_SCHEDULE_OK);
    assert(a1_schedule_encode(&schedule, encoded, sizeof(encoded),
                              &written) == A1_SCHEDULE_OK);
    assert(written == sizeof(encoded));
    assert(memcmp(encoded, "SCHE", 4u) == 0);
    assert(encoded[4] == 1u && encoded[16] == 2u);
    assert(a1_schedule_decode(encoded, sizeof(encoded), &decoded,
                              &consumed) == A1_SCHEDULE_OK);
    assert(consumed == sizeof(encoded));
    assert(decoded.current_seconds == schedule.current_seconds);
    assert(decoded.entries[0].schedule_id == 7u);
    assert(decoded.entries[1].reserved[5] == 0xa5u);

    assert(a1_schedule_next_start(&decoded, UINT64_C(1700000000), &next));
    assert(next == UINT64_C(1700000100));
    assert(a1_schedule_timer_mode(&decoded, UINT64_C(1700000000)) ==
           A1_SCHEDULE_TIMER_PRECISE);
    assert(a1_schedule_timer_mode(&decoded, UINT64_C(1700000179)) ==
           A1_SCHEDULE_TIMER_COARSE);
    assert(a1_schedule_has_unexpired(&decoded, UINT64_C(1700000399)));
    assert(!a1_schedule_has_unexpired(&decoded, UINT64_C(1700000400)));
    assert(a1_schedule_timer_mode(&decoded, UINT64_C(1700000400)) ==
           A1_SCHEDULE_TIMER_NORMAL);

    encoded[20] ^= 1u;
    assert(a1_schedule_decode(encoded, sizeof(encoded), &decoded,
                              &consumed) == A1_SCHEDULE_BAD_CHECKSUM);
    encoded[20] ^= 1u;
    schedule.entries[0].end_seconds = schedule.current_seconds;
    assert(a1_schedule_validate_set(&schedule) == A1_SCHEDULE_INVALID_ENTRY);
    schedule.count = 0;
    assert(a1_schedule_validate_set(&schedule) == A1_SCHEDULE_OK);
    assert(a1_schedule_storage_size(&schedule) == 0u);
}

static void test_gray_switch_policy(void)
{
    a1_gray_switch_state_t state;
    a1_gray_switch_update_t update = {0};
    a1_gray_switch_effect_t effect;
    a1_gray_control_plan_t plan;

    a1_gray_switch_init(&state);
    assert(state.stream_record && !a1_gray_switch_remark_enabled(&state));
    update.has_log_record = true;
    update.log_record = 1;
    update.has_stream_record = true;
    update.stream_record = 0;
    update.has_remark = true;
    update.remark = A1_GRAY_REMARK_ENABLED_VALUE;
    a1_gray_switch_apply(&state, &update, &effect);
    assert(state.log_record && !state.stream_record);
    assert(effect.stream_record_changed && effect.stream_record_event_value == 2);
    assert(a1_gray_switch_remark_enabled(&state));

    memset(&update, 0, sizeof(update));
    a1_gray_switch_apply(&state, &update, &effect);
    assert(state.log_record && !state.stream_record && state.remark == 0);

    plan = a1_gray_control_plan(1, NULL);
    assert(plan.action == A1_GRAY_CONTROL_REBOOT && plan.respond_before_action);
    plan = a1_gray_control_plan(2, NULL);
    assert(plan.action == A1_GRAY_CONTROL_SHUTDOWN &&
           strcmp(plan.shell_command, "shutdown") == 0);
    plan = a1_gray_control_plan(3, "adbd &");
    assert(plan.action == A1_GRAY_CONTROL_SHELL_COMMAND &&
           strcmp(plan.shell_command, "adbd &") == 0);
    plan = a1_gray_control_plan(5, NULL);
    assert(plan.action == A1_GRAY_CONTROL_DELETE_OLDEST_RECORDING &&
           plan.respond_after_action);
    assert(a1_gray_control_plan(99, NULL).action == A1_GRAY_CONTROL_INVALID);
}

static void test_companion_core_manifest(void)
{
    uint8_t resources[2u * A1_RPTUN_RESOURCE_SIZE] = {0};
    const a1_companion_core_t *core;
    const uint8_t *found;

    assert(a1_companion_manifest_validate() == 0);
    assert(a1_companion_core_count() == A1_COMPANION_CORE_COUNT);
    assert(a1_companion_core_at(A1_COMPANION_CORE_COUNT) == NULL);

    core = a1_companion_core_find(A1_COMPANION_DSPC0);
    assert(core != NULL && strcmp(core->rpmsg_remote_name, "audio") == 0);
    assert(core->rmt_ipc_core == 2u && core->resource_id == 3u);
    assert(core->ap_local_arena_address == UINT32_C(0x201e057c));
    assert(core->rx_irq_line == 76u && core->tx_irq_line == 79u);
    assert(core->stock_ops.rx_irq_entry_address == UINT32_C(0x10241de9));
    assert(core->stock_ops.tx_irq_entry_address == UINT32_C(0x10241ddd));
    assert(core->channel0_message_slots == 5u &&
           !core->wake_lock_enabled);

    core = a1_companion_core_find(A1_COMPANION_BTHC0);
    assert(core != NULL && strcmp(core->rpmsg_remote_name, "bth") == 0);
    assert(core->rmt_ipc_core == 3u && core->resource_id == 1u);
    assert(core->ap_local_arena_address == UINT32_C(0x23c00000));
    assert(core->rx_irq_line == 96u && core->tx_irq_line == 94u);
    assert(core->stock_ops.peer_tx_irq_set_address ==
           UINT32_C(0x10241faf));
    assert(core->channel0_message_slots == 8u && core->wake_lock_enabled);

    core = a1_companion_core_find(A1_COMPANION_M55C1);
    assert(core != NULL && strcmp(core->rpmsg_remote_name, "apc1") == 0);
    assert(core->rmt_ipc_core == 1u && core->resource_id == 0u);
    assert(core->resource_table_address == UINT32_C(0x23c0b000));
    assert(core->rx_irq_line == 88u && core->tx_irq_line == 91u);
    assert(core->stock_ops.rx_irq_suspend_address ==
           UINT32_C(0x10241d39));
    assert(core->peer_config_pointer_address == UINT32_C(0x21dbffa4));
    assert(core->local_config_pointer_address == UINT32_C(0x21dbffa0));

    assert(a1_rptun_local_arena_size(8u, 512u, 8u) == 0x10e0u);
    assert(a1_rptun_local_arena_size(0u, 512u, 8u) == 0u);
    assert(a1_rptun_local_arena_size(8u, 512u, 3u) == 0u);

    write_le32_test(resources + A1_RPTUN_RESOURCE_ID_OFFSET, 3u);
    write_le32_test(resources + A1_RPTUN_RESOURCE_SIZE +
                        A1_RPTUN_RESOURCE_ID_OFFSET,
                    1u);
    found = a1_rptun_resource_find(resources, sizeof(resources), 1u);
    assert(found == resources + A1_RPTUN_RESOURCE_SIZE);
    assert(a1_rptun_resource_find(resources, sizeof(resources), 9u) == NULL);
    assert(a1_rptun_resource_find(resources, sizeof(resources) - 1u, 1u) ==
           NULL);
}

typedef struct {
    bool active[2];
    bool rx_enabled;
    bool tx_enabled;
    bool busy;
    size_t receive_limit;
    size_t received_bytes;
    size_t transmitted_bytes;
    unsigned irq_inits;
    unsigned peer_tx_sets;
    unsigned local_tx_clears;
    unsigned rx_done_calls;
    unsigned rx_suspends;
    unsigned rx_resumes;
    unsigned barriers;
} fake_rmt_ipc_t;

static void fake_rmt_irq_init(void *context, uint8_t channel)
{
    fake_rmt_ipc_t *fake = context;
    assert(channel == 0u);
    ++fake->irq_inits;
}

static void fake_rmt_peer_tx_set(void *context, uint8_t channel)
{
    fake_rmt_ipc_t *fake = context;
    assert(channel == 0u);
    ++fake->peer_tx_sets;
}

static void fake_rmt_local_tx_clear(void *context, uint8_t channel)
{
    fake_rmt_ipc_t *fake = context;
    assert(channel == 0u);
    ++fake->local_tx_clears;
}

static void fake_rmt_rx_done(void *context, uint8_t channel)
{
    fake_rmt_ipc_t *fake = context;
    assert(channel == 0u);
    ++fake->rx_done_calls;
}

static bool fake_rmt_irq_active(void *context,
                                uint8_t channel,
                                a1_rmt_ipc_irq_type_t type)
{
    fake_rmt_ipc_t *fake = context;
    assert(channel == 0u);
    return fake->active[type];
}

static void fake_rmt_rx_suspend(void *context, uint8_t channel)
{
    fake_rmt_ipc_t *fake = context;
    assert(channel == 0u);
    ++fake->rx_suspends;
}

static void fake_rmt_rx_resume(void *context, uint8_t channel)
{
    fake_rmt_ipc_t *fake = context;
    assert(channel == 0u);
    ++fake->rx_resumes;
}

static void fake_rmt_rx_enable(void *context,
                               uint8_t channel,
                               bool enabled)
{
    fake_rmt_ipc_t *fake = context;
    assert(channel == 0u);
    fake->rx_enabled = enabled;
}

static void fake_rmt_tx_enable(void *context,
                               uint8_t channel,
                               bool enabled)
{
    fake_rmt_ipc_t *fake = context;
    assert(channel == 0u);
    fake->tx_enabled = enabled;
}

static void fake_rmt_barrier(void *context)
{
    fake_rmt_ipc_t *fake = context;
    ++fake->barriers;
}

static void fake_rmt_busy(void *context, uint8_t channel, bool busy)
{
    fake_rmt_ipc_t *fake = context;
    assert(channel == 0u);
    fake->busy = busy;
}

static size_t fake_rmt_receive(void *context,
                               const uint8_t *data,
                               size_t length)
{
    fake_rmt_ipc_t *fake = context;
    size_t consumed = length < fake->receive_limit ? length
                                                    : fake->receive_limit;
    assert(data != NULL);
    fake->received_bytes += consumed;
    return consumed;
}

static void fake_rmt_transmit(void *context,
                              const uint8_t *data,
                              size_t length)
{
    fake_rmt_ipc_t *fake = context;
    assert(data != NULL);
    fake->transmitted_bytes += length;
}

static void test_rmt_ipc_channel(void)
{
    static const uint8_t first[] = {1u, 2u, 3u};
    static const uint8_t second[] = {4u, 5u};
    static const uint8_t third[] = {6u};
    static const uint8_t inbound[] = {7u, 8u, 9u, 10u, 11u};
    fake_rmt_ipc_t fake = {0};
    a1_rmt_ipc_send_slot_t slots[3];
    a1_rmt_ipc_channel_t ipc;
    a1_rmt_ipc_message_t peer = {NULL, sizeof(inbound), inbound};
    a1_rmt_ipc_port_t port = {
        &fake,
        fake_rmt_irq_init,
        fake_rmt_peer_tx_set,
        fake_rmt_local_tx_clear,
        fake_rmt_rx_done,
        fake_rmt_irq_active,
        fake_rmt_rx_suspend,
        fake_rmt_rx_resume,
        fake_rmt_rx_enable,
        fake_rmt_tx_enable,
        fake_rmt_barrier,
        fake_rmt_busy
    };

    assert(a1_rmt_ipc_channel_init(&ipc, &port, 0u, slots, 3u) == 0);
    fake.receive_limit = SIZE_MAX;
    assert(a1_rmt_ipc_open(&ipc, fake_rmt_receive, fake_rmt_transmit,
                           &fake, false) == 0);
    assert(fake.irq_inits == 1u && fake.tx_enabled && !fake.rx_enabled);
    assert(a1_rmt_ipc_start_receive(&ipc) == 0 && fake.rx_enabled);

    assert(a1_rmt_ipc_send(&ipc, first, sizeof(first)) == 0);
    assert(a1_rmt_ipc_send(&ipc, second, sizeof(second)) == 1);
    assert(a1_rmt_ipc_send(&ipc, third, sizeof(third)) == 2);
    assert(a1_rmt_ipc_send(&ipc, third, sizeof(third)) == -2);
    assert(fake.peer_tx_sets == 1u && fake.busy && fake.barriers == 1u);

    fake.active[A1_RMT_IPC_IRQ_RECV_DONE] = true;
    assert(a1_rmt_ipc_handle_tx_irq(&ipc) == 1);
    assert(fake.transmitted_bytes == sizeof(first));
    assert(fake.peer_tx_sets == 2u && a1_rmt_ipc_tx_active(&ipc, 1u));
    assert(a1_rmt_ipc_handle_tx_irq(&ipc) == 2);
    assert(fake.transmitted_bytes == sizeof(first) + sizeof(second) +
                                       sizeof(third));
    assert(!fake.busy && fake.local_tx_clears == 2u);

    fake.active[A1_RMT_IPC_IRQ_SEND_IND] = true;
    fake.receive_limit = 2u;
    assert(a1_rmt_ipc_handle_rx_irq(&ipc, &peer) == 1);
    assert(fake.received_bytes == 2u && fake.rx_done_calls == 0u);
    assert(ipc.receive_pending.length == 3u);
    fake.receive_limit = SIZE_MAX;
    assert(a1_rmt_ipc_start_receive(&ipc) == 0);
    assert(fake.rx_resumes == 1u);
    assert(a1_rmt_ipc_handle_rx_irq(&ipc, NULL) == 1);
    assert(fake.received_bytes == sizeof(inbound));
    assert(fake.rx_done_calls == 1u && ipc.receive_pending.data == NULL);

    a1_rmt_ipc_close(&ipc);
    assert(!ipc.opened && !fake.rx_enabled && !fake.tx_enabled);
    assert(fake.irq_inits == 2u);
}

int main(void)
{
    test_frame_codec();
    test_json_reader();
    test_json_writer();
    test_wire_responses();
    test_wire_requests();
    test_firmware_version_policy();
    test_inbound_transfer_state();
    test_flash_layout();
    test_boot_image_headers();
    test_wifi_tcp_transport();
    test_wifi_tcp_ota_block();
    test_recording_policy();
    test_ble_link_policy();
    test_stream_transport();
    test_router_registry();
    test_persistence_and_service_rollback();
    test_protocol_service_session();
    test_ble_endpoint_stream();
    test_audio_and_ai_key();
    test_auth_and_session();
    test_button_policy();
    test_display_state_names();
    test_device_info();
    test_system_control_plan();
    test_remark_flow();
    test_memo_container();
    test_raw_transfer();
    test_file_sync();
    test_usb_hid();
    test_audio_index();
    test_live_audio_and_ut();
    test_audio_container();
    test_wifi_state();
    test_schedule_storage_and_selection();
    test_gray_switch_policy();
    test_companion_core_manifest();
    test_rmt_ipc_channel();
    puts("a1_core_tests: all focused protocol tests passed");
    return 0;
}
