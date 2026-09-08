#include "a1/protocol_service.h"

#include <string.h>

#include "a1/system_control.h"
#include "a1/wire_requests.h"

enum {
    A1_RESPONSE_OK = 200,
    A1_RESPONSE_AUTH_REQUIRED = 405,
    A1_RESPONSE_INVALID = 500,
    A1_RESPONSE_MISSING = 521,
    A1_RESPONSE_JSON = 541,
    A1_RESPONSE_INTERNAL = 542,
    A1_RESPONSE_BUFFER_CAPACITY = 2048
};

static int send_json(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    uint16_t code,
    const char *json,
    size_t length)
{
    const a1_platform_t *platform = service->runtime.platform;
    if (platform == NULL) {
        return -1;
    }
    if (platform->send_json_response != NULL) {
        return platform->send_json_response(
            platform->context, command, message_id, json, length);
    }
    if (platform->send_response != NULL) {
        return platform->send_response(
            platform->context, command, message_id, code, json);
    }
    return -1;
}

static a1_route_result_t send_code(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    uint16_t code,
    a1_route_result_t result)
{
    char json[32];
    size_t length;
    if (a1_wire_encode_code(json, sizeof(json), code, &length) != 0 ||
        send_json(service, command, message_id, code, json, length) != 0) {
        return A1_ROUTE_PLATFORM_ERROR;
    }
    return result;
}

static a1_route_result_t send_code_with_type(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    uint16_t code,
    uint32_t type,
    a1_route_result_t result)
{
    char json[48];
    size_t length;
    if (a1_wire_encode_code_with_type(json, sizeof(json), code, type,
                                      &length) != 0 ||
        send_json(service, command, message_id, code, json, length) != 0) {
        return A1_ROUTE_PLATFORM_ERROR;
    }
    return result;
}

static uint16_t wire_error_code(int result)
{
    if (result == A1_WIRE_INVALID_JSON) {
        return A1_RESPONSE_JSON;
    }
    if (result == A1_WIRE_MISSING_FIELD) {
        return A1_RESPONSE_MISSING;
    }
    return A1_RESPONSE_INVALID;
}

static int is_connected(const a1_protocol_service_t *service)
{
    return service->runtime.logical_session_connected &&
           service->session.has_active_did;
}

static a1_route_result_t handle_random(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    char challenge[A1_CHALLENGE_LENGTH + 1u];
    char json[80];
    size_t length;
    if (a1_wire_decode_empty_object(body, body_length) != A1_WIRE_OK) {
        return send_code(service, command, message_id, A1_RESPONSE_JSON,
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (a1_session_create_challenge(&service->session,
                                    service->runtime.platform,
                                    challenge) != 0 ||
        a1_wire_encode_random(json, sizeof(json), challenge, &length) != 0) {
        return send_code(service, command, message_id, A1_RESPONSE_INTERNAL,
                         A1_ROUTE_PLATFORM_ERROR);
    }
    return send_json(service, command, message_id, A1_RESPONSE_OK,
                     json, length) == 0
        ? A1_ROUTE_OK
        : A1_ROUTE_PLATFORM_ERROR;
}

static a1_route_result_t handle_connect(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_wire_connect_request_t decoded;
    a1_connect_request_t request;
    a1_session_code_t code;
    char json[A1_RESPONSE_BUFFER_CAPACITY];
    size_t length;
    int result = a1_wire_decode_connect(body, body_length, &decoded);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    request.did = decoded.did;
    request.token = decoded.token;
    request.token_length = strlen(decoded.token);
    request.timestamp = decoded.has_timestamp ? decoded.timestamp : 0;
    request.model = decoded.has_model ? decoded.model : NULL;
    request.sdk_version = decoded.has_sdk_version ? decoded.sdk_version : NULL;
    code = a1_session_connect(&service->session, &service->runtime, &request);
    if (code != A1_SESSION_CODE_OK) {
        return send_code(service, command, message_id, (uint16_t)code,
                         A1_ROUTE_REJECTED);
    }
    if (a1_wire_encode_connect(json, sizeof(json), code,
                               &service->capabilities, &length) != 0 ||
        send_json(service, command, message_id, code, json, length) != 0) {
        return A1_ROUTE_PLATFORM_ERROR;
    }
    return A1_ROUTE_OK;
}

static a1_route_result_t handle_disconnect(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    int64_t did;
    a1_session_code_t code;
    if (a1_wire_decode_empty_object(body, body_length) != A1_WIRE_OK) {
        return send_code(service, command, message_id, A1_RESPONSE_JSON,
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (!is_connected(service)) {
        return send_code(service, command, message_id,
                         A1_RESPONSE_AUTH_REQUIRED, A1_ROUTE_REJECTED);
    }
    did = service->session.active_did;
    code = a1_session_disconnect(&service->session, &service->runtime, did);
    return send_code(service, command, message_id, (uint16_t)code,
                     code == A1_SESSION_CODE_OK ? A1_ROUTE_OK
                                                : A1_ROUTE_REJECTED);
}

static a1_route_result_t require_connected(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id)
{
    return is_connected(service)
        ? A1_ROUTE_OK
        : send_code(service, command, message_id,
                    A1_RESPONSE_AUTH_REQUIRED, A1_ROUTE_REJECTED);
}

static a1_route_result_t handle_audio(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_wire_audio_request_t request;
    a1_audio_settings_t previous_settings;
    char json[A1_RESPONSE_BUFFER_CAPACITY];
    size_t json_length;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_audio(body, body_length, &request);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (request.action == A1_AUDIO_ACTION_SET) {
        static const a1_audio_setting_key_t KEYS[] = {
            A1_AUDIO_SETTING_UPLOAD_STREAM,
            A1_AUDIO_SETTING_DELETE_AFTER_UPLOAD,
            A1_AUDIO_SETTING_MODE,
            A1_AUDIO_SETTING_AES,
            A1_AUDIO_SETTING_INCOGNITO_MODE,
            A1_AUDIO_SETTING_FORCE_SYNC_INCOGNITO,
            A1_AUDIO_SETTING_AI_KEY_OPTION,
            A1_AUDIO_SETTING_STREAM_RECORD
        };
        size_t index;
        previous_settings = service->audio_settings;
        result = 0;
        for (index = 0u; index < sizeof(KEYS) / sizeof(KEYS[0]); ++index) {
            a1_audio_setting_key_t key = KEYS[index];
            int value = 0;
            if ((request.settings.present_mask & A1_AUDIO_SETTING_PRESENT(key)) == 0u) {
                continue;
            }
            switch (key) {
            case A1_AUDIO_SETTING_UPLOAD_STREAM:
                value = request.settings.upload_stream;
                break;
            case A1_AUDIO_SETTING_DELETE_AFTER_UPLOAD:
                value = request.settings.delete_after_upload;
                break;
            case A1_AUDIO_SETTING_MODE:
                value = request.settings.mode;
                break;
            case A1_AUDIO_SETTING_AES:
                value = request.settings.aes;
                break;
            case A1_AUDIO_SETTING_INCOGNITO_MODE:
                value = request.settings.incognito_mode;
                break;
            case A1_AUDIO_SETTING_FORCE_SYNC_INCOGNITO:
                value = request.settings.force_sync_incognito;
                break;
            case A1_AUDIO_SETTING_AI_KEY_OPTION:
                value = request.settings.ai_key_option;
                break;
            case A1_AUDIO_SETTING_STREAM_RECORD:
                value = request.settings.stream_record;
                break;
            default:
                result = -1;
                break;
            }
            if (result != 0 ||
                a1_audio_setting_apply(&service->audio_settings, key, value) != 0) {
                result = -1;
                break;
            }
        }
        if (result == 0 && service->persistence != NULL &&
            a1_persistence_store_audio_update(service->persistence,
                                              &request.settings) != 0) {
            service->audio_settings = previous_settings;
            return send_code(service, command, message_id,
                             A1_RESPONSE_INTERNAL,
                             A1_ROUTE_PLATFORM_ERROR);
        }
    } else if (request.action == A1_AUDIO_ACTION_GET) {
        if (a1_wire_encode_audio_settings(
                json, sizeof(json), A1_RESPONSE_OK,
                &service->audio_settings, &json_length) != 0 ||
            send_json(service, command, message_id, A1_RESPONSE_OK,
                      json, json_length) != 0) {
            return A1_ROUTE_PLATFORM_ERROR;
        }
        return A1_ROUTE_OK;
    } else {
        result = a1_audio_apply_action(&service->runtime, request.action);
    }
    return send_code(service, command, message_id,
                     result == 0 ? A1_RESPONSE_OK : A1_RESPONSE_INVALID,
                     result == 0 ? A1_ROUTE_OK : A1_ROUTE_REJECTED);
}

static a1_route_result_t handle_voiceprint(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_voiceprint_action_t action;
    const a1_platform_t *platform = service->runtime.platform;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_voiceprint(body, body_length, &action);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (action == A1_VOICEPRINT_START) {
        if (service->runtime.audio_state != A1_AUDIO_IDLE ||
            platform == NULL || platform->start_live_stream == NULL) {
            result = -1;
        } else {
            result = platform->start_live_stream(platform->context);
            if (result == 0) {
                service->runtime.audio_state = A1_AUDIO_LIVE_STREAM;
            }
        }
    } else if (service->runtime.audio_state != A1_AUDIO_LIVE_STREAM ||
               platform == NULL || platform->stop_live_stream == NULL) {
        result = -1;
    } else {
        result = platform->stop_live_stream(platform->context);
        if (result == 0) {
            service->runtime.audio_state = A1_AUDIO_IDLE;
        }
    }
    return send_code(service, command, message_id,
                     result == 0 ? A1_RESPONSE_OK : A1_RESPONSE_INVALID,
                     result == 0 ? A1_ROUTE_OK : A1_ROUTE_REJECTED);
}

static a1_route_result_t handle_open_ap(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_wifi_mode_t mode;
    char json[A1_RESPONSE_BUFFER_CAPACITY];
    size_t length;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_open_ap(body, body_length, &mode);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    result = a1_wifi_open(&service->wifi, service->runtime.platform, mode,
                          service->identity.device_name);
    if (result != 0) {
        return send_code(service, command, message_id, 543u, A1_ROUTE_REJECTED);
    }
    if (a1_wire_encode_wifi(json, sizeof(json), A1_RESPONSE_OK,
                            &service->wifi, &length) != 0 ||
        send_json(service, command, message_id, A1_RESPONSE_OK,
                  json, length) != 0) {
        return A1_ROUTE_PLATFORM_ERROR;
    }
    return A1_ROUTE_OK;
}

static a1_route_result_t handle_close_ap(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    if (a1_wire_decode_empty_object(body, body_length) != A1_WIRE_OK) {
        return send_code(service, command, message_id, A1_RESPONSE_JSON,
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    result = a1_wifi_close(&service->wifi, service->runtime.platform);
    return send_code(service, command, message_id,
                     result == 0 ? A1_RESPONSE_OK : A1_RESPONSE_INVALID,
                     result == 0 ? A1_ROUTE_OK : A1_ROUTE_PLATFORM_ERROR);
}

static a1_route_result_t handle_device_info(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    char json[A1_RESPONSE_BUFFER_CAPACITY];
    size_t length;
    if (a1_wire_decode_empty_object(body, body_length) != A1_WIRE_OK) {
        return send_code(service, command, message_id, A1_RESPONSE_JSON,
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (a1_wire_encode_device_info(json, sizeof(json), A1_RESPONSE_OK,
                                   &service->identity, &length) != 0 ||
        send_json(service, command, message_id, A1_RESPONSE_OK,
                  json, length) != 0) {
        return A1_ROUTE_PLATFORM_ERROR;
    }
    return A1_ROUTE_OK;
}

static a1_route_result_t handle_audio_status(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    char json[A1_RESPONSE_BUFFER_CAPACITY];
    size_t length;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    if (a1_wire_decode_empty_object(body, body_length) != A1_WIRE_OK) {
        return send_code(service, command, message_id, A1_RESPONSE_JSON,
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (a1_wire_encode_audio_status(json, sizeof(json), A1_RESPONSE_OK,
                                    &service->status,
                                    service->identity.firmware_version,
                                    &length) != 0 ||
        send_json(service, command, message_id, A1_RESPONSE_OK,
                  json, length) != 0) {
        return A1_ROUTE_PLATFORM_ERROR;
    }
    return A1_ROUTE_OK;
}

static a1_route_result_t handle_firmware_query(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_wire_firmware_query_t request;
    const a1_platform_t *platform = service->runtime.platform;
    bool upgrade;
    uint32_t offset = 0u;
    char json[A1_RESPONSE_BUFFER_CAPACITY];
    size_t length;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_firmware_query(body, body_length, &request);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    upgrade = request.has_new_version &&
        a1_firmware_version_is_newer(request.new_version,
                                     service->identity.firmware_version);
    service->firmware_query_received = true;
    service->firmware_target_version[0] = '\0';
    if (upgrade) {
        memcpy(service->firmware_target_version, request.new_version,
               strlen(request.new_version) + 1u);
        if (platform != NULL && platform->get_ota_resume_offset != NULL &&
            platform->get_ota_resume_offset(platform->context,
                                            request.new_version,
                                            &offset) != 0) {
            return send_code(service, command, message_id,
                             A1_RESPONSE_INTERNAL,
                             A1_ROUTE_PLATFORM_ERROR);
        }
    }
    service->firmware_resume_offset = upgrade ? offset : 0u;
    if (a1_wire_encode_firmware_query(
            json, sizeof(json), A1_RESPONSE_OK, upgrade,
            service->identity.firmware_version, offset, &length) != 0 ||
        send_json(service, command, message_id, A1_RESPONSE_OK,
                  json, length) != 0) {
        return A1_ROUTE_PLATFORM_ERROR;
    }
    return A1_ROUTE_OK;
}

static uint16_t file_header_decode_code(int result)
{
    switch (result) {
    case A1_WIRE_FILE_HEADER_INVALID_JSON:
        return A1_RESPONSE_JSON;
    case A1_WIRE_FILE_HEADER_VERIFY_FIELD:
        return 501u;
    case A1_WIRE_FILE_HEADER_ATTRS_FIELD:
        return 506u;
    case A1_WIRE_FILE_HEADER_VERSION_FIELD:
        return 504u;
    case A1_WIRE_FILE_HEADER_MISSING_SIZE:
    case A1_WIRE_FILE_HEADER_INVALID_VALUE:
    default:
        return A1_RESPONSE_MISSING;
    }
}

static a1_route_result_t handle_inbound_header(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    static const char OTA_ATTRS[] = "ota@bin";
    static const char IMAGE_ATTRS[] = "image@bmp@usrinfo";
    a1_wire_file_header_request_t request;
    const a1_platform_t *platform = service->runtime.platform;
    a1_inbound_type_t type = A1_INBOUND_OTA;
    uint32_t resume_offset = 0u;
    uint16_t code = A1_RESPONSE_OK;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_file_header(body, body_length, &request);
    if (result != A1_WIRE_FILE_HEADER_OK) {
        code = file_header_decode_code(result);
        return send_code_with_type(service, command, message_id, code, 0u,
                                   A1_ROUTE_INVALID_ARGUMENT);
    }
    if (strcmp(request.attrs, OTA_ATTRS) == 0) {
        if (!request.has_version) {
            code = 504u;
        } else if (!a1_firmware_version_is_newer(
                       request.version, service->identity.firmware_version)) {
            code = 502u;
        } else if (!service->firmware_query_received) {
            code = 503u;
        } else {
            resume_offset = service->firmware_resume_offset;
            service->firmware_query_received = false;
        }
    } else if (strncmp(request.attrs, IMAGE_ATTRS,
                       sizeof(IMAGE_ATTRS) - 1u) == 0) {
        type = A1_INBOUND_USER_IMAGE;
    } else {
        code = 505u;
    }
    if (code != A1_RESPONSE_OK) {
        return send_code_with_type(service, command, message_id, code,
                                   (uint32_t)type, A1_ROUTE_REJECTED);
    }
    if (platform == NULL || platform->begin_inbound_file == NULL ||
        platform->write_inbound_file == NULL ||
        platform->finish_inbound_file == NULL ||
        platform->cancel_inbound_file == NULL) {
        return send_code_with_type(service, command, message_id,
                                   A1_RESPONSE_INTERNAL, (uint32_t)type,
                                   A1_ROUTE_PLATFORM_ERROR);
    }
    if (service->inbound_transfer.active) {
        if (platform->cancel_inbound_file(platform->context) != 0) {
            return send_code_with_type(service, command, message_id,
                                       A1_RESPONSE_INTERNAL, (uint32_t)type,
                                       A1_ROUTE_PLATFORM_ERROR);
        }
        a1_inbound_transfer_cancel(&service->inbound_transfer);
    }
    if (a1_inbound_transfer_begin(&service->inbound_transfer, type,
                                  request.size, resume_offset,
                                  request.verify_hash) < 0) {
        return send_code_with_type(service, command, message_id,
                                   A1_RESPONSE_INTERNAL, (uint32_t)type,
                                   A1_ROUTE_PLATFORM_ERROR);
    }
    if (platform->begin_inbound_file(
            platform->context, (int)type, request.size, resume_offset,
            request.verify_hash, request.attrs,
            request.has_version ? request.version : NULL) != 0) {
        (void)platform->cancel_inbound_file(platform->context);
        a1_inbound_transfer_cancel(&service->inbound_transfer);
        return send_code_with_type(service, command, message_id,
                                   A1_RESPONSE_INTERNAL, (uint32_t)type,
                                   A1_ROUTE_PLATFORM_ERROR);
    }
    if (service->inbound_transfer.complete) {
        uint32_t computed_hash;
        if (platform->finish_inbound_file(platform->context,
                                          &computed_hash) != 0 ||
            computed_hash != service->inbound_transfer.expected_verify_hash) {
            (void)platform->cancel_inbound_file(platform->context);
            a1_inbound_transfer_cancel(&service->inbound_transfer);
            return send_code_with_type(service, command, message_id,
                                       A1_RESPONSE_INTERNAL, (uint32_t)type,
                                       A1_ROUTE_REJECTED);
        }
        service->inbound_transfer.verified = true;
    }
    return send_code_with_type(service, command, message_id, A1_RESPONSE_OK,
                               (uint32_t)type, A1_ROUTE_OK);
}

static a1_route_result_t handle_inbound_block(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_inbound_block_view_t block;
    a1_inbound_transfer_t next_transfer;
    const a1_platform_t *platform = service->runtime.platform;
    uint32_t type = service->inbound_transfer.type;
    uint32_t computed_hash;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_inbound_block_decode(body, body_length, &block);
    if (result == -4) {
        return send_code_with_type(service, command, message_id, 403u, type,
                                   A1_ROUTE_REJECTED);
    }
    if (result != 0 || platform == NULL ||
        platform->write_inbound_file == NULL ||
        platform->finish_inbound_file == NULL ||
        platform->cancel_inbound_file == NULL) {
        return send_code_with_type(service, command, message_id,
                                   A1_RESPONSE_INTERNAL, type,
                                   A1_ROUTE_PLATFORM_ERROR);
    }
    next_transfer = service->inbound_transfer;
    result = a1_inbound_transfer_commit_block(&next_transfer, &block);
    if (result < 0) {
        return send_code_with_type(service, command, message_id,
                                   A1_RESPONSE_INVALID, type,
                                   A1_ROUTE_REJECTED);
    }
    if (platform->write_inbound_file(platform->context, block.data,
                                     block.data_length) != 0) {
        (void)platform->cancel_inbound_file(platform->context);
        a1_inbound_transfer_cancel(&service->inbound_transfer);
        return send_code_with_type(service, command, message_id,
                                   A1_RESPONSE_INTERNAL, type,
                                   A1_ROUTE_PLATFORM_ERROR);
    }
    service->inbound_transfer = next_transfer;
    if (result == 1) {
        if (platform->finish_inbound_file(platform->context,
                                          &computed_hash) != 0 ||
            computed_hash != service->inbound_transfer.expected_verify_hash) {
            (void)platform->cancel_inbound_file(platform->context);
            a1_inbound_transfer_cancel(&service->inbound_transfer);
            return send_code_with_type(service, command, message_id,
                                       A1_RESPONSE_INTERNAL, type,
                                       A1_ROUTE_REJECTED);
        }
        service->inbound_transfer.verified = true;
    }
    return send_code_with_type(service, command, message_id, A1_RESPONSE_OK,
                               type, A1_ROUTE_OK);
}

static a1_route_result_t handle_schedule(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_wire_schedule_request_t request;
    char json[A1_RESPONSE_BUFFER_CAPACITY];
    size_t length;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_schedule(body, body_length, &request);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (request.action == A1_WIRE_SCHEDULE_SET) {
        a1_schedule_t previous_schedule = service->schedule;
        service->schedule = request.schedule;
        if (service->persistence != NULL &&
            a1_persistence_store_schedule(service->persistence,
                                          &service->schedule) != 0) {
            service->schedule = previous_schedule;
            return send_code(service, command, message_id,
                             A1_RESPONSE_INTERNAL,
                             A1_ROUTE_PLATFORM_ERROR);
        }
        return send_code(service, command, message_id, A1_RESPONSE_OK,
                         A1_ROUTE_OK);
    }
    if (a1_wire_encode_schedule(json, sizeof(json), A1_RESPONSE_OK,
                                &service->schedule, &length) != 0 ||
        send_json(service, command, message_id, A1_RESPONSE_OK,
                  json, length) != 0) {
        return A1_ROUTE_PLATFORM_ERROR;
    }
    return A1_ROUTE_OK;
}

static a1_route_result_t handle_file_list(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_wire_file_list_request_t request;
    const a1_platform_t *platform = service->runtime.platform;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_file_list(body, body_length, &request);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (platform == NULL || platform->start_file_list == NULL ||
        platform->start_file_list(platform->context, request.start_fid,
                                  request.end_fid, request.recently,
                                  message_id) != 0) {
        return send_code(service, command, message_id, A1_RESPONSE_INVALID,
                         A1_ROUTE_PLATFORM_ERROR);
    }
    return A1_ROUTE_OK;
}

static a1_route_result_t handle_file_sync(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_wire_file_sync_request_t request;
    const a1_platform_t *platform = service->runtime.platform;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_file_sync(body, body_length, &request);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (platform == NULL || platform->start_file_sync == NULL ||
        platform->start_file_sync(platform->context, request.fid,
                                  request.offset, request.progress,
                                  message_id) != 0) {
        return send_code(service, command, message_id, A1_RESPONSE_INVALID,
                         A1_ROUTE_PLATFORM_ERROR);
    }
    return A1_ROUTE_OK;
}

static a1_route_result_t handle_file_cancel(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    const a1_platform_t *platform = service->runtime.platform;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    if (a1_wire_decode_empty_object(body, body_length) != A1_WIRE_OK) {
        return send_code(service, command, message_id, A1_RESPONSE_JSON,
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    result = platform != NULL && platform->cancel_file_sync != NULL
        ? platform->cancel_file_sync(platform->context)
        : -1;
    return send_code(service, command, message_id,
                     result == 0 ? A1_RESPONSE_OK : A1_RESPONSE_INVALID,
                     result == 0 ? A1_ROUTE_OK : A1_ROUTE_PLATFORM_ERROR);
}

static a1_route_result_t handle_file_delete(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    const a1_platform_t *platform = service->runtime.platform;
    uint64_t fid;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_file_fid(body, body_length, &fid);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (platform == NULL || platform->delete_recording == NULL) {
        return send_code(service, command, message_id, A1_RESPONSE_INVALID,
                         A1_ROUTE_PLATFORM_ERROR);
    }
    /* Stock acknowledges before deleting the database/file pair. */
    if (send_code(service, command, message_id, A1_RESPONSE_OK,
                  A1_ROUTE_OK) != A1_ROUTE_OK) {
        return A1_ROUTE_PLATFORM_ERROR;
    }
    return platform->delete_recording(platform->context, fid) == 0
        ? A1_ROUTE_OK
        : A1_ROUTE_PLATFORM_ERROR;
}

static a1_route_result_t handle_raw_transfer(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_wire_raw_transfer_request_t request;
    const a1_platform_t *platform = service->runtime.platform;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_raw_transfer(body, body_length, &request);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (platform == NULL || platform->start_raw_transfer == NULL ||
        platform->start_raw_transfer(platform->context, request.path,
                                     request.offset, message_id) != 0) {
        return send_code(service, command, message_id, A1_RESPONSE_INVALID,
                         A1_ROUTE_PLATFORM_ERROR);
    }
    return A1_ROUTE_OK;
}

static a1_route_result_t handle_raw_cancel(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    const a1_platform_t *platform = service->runtime.platform;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    if (a1_wire_decode_empty_object(body, body_length) != A1_WIRE_OK) {
        return send_code(service, command, message_id, A1_RESPONSE_JSON,
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    result = platform != NULL && platform->cancel_raw_transfer != NULL
        ? platform->cancel_raw_transfer(platform->context)
        : -1;
    return send_code(service, command, message_id,
                     result == 0 ? A1_RESPONSE_OK : A1_RESPONSE_INVALID,
                     result == 0 ? A1_ROUTE_OK : A1_ROUTE_PLATFORM_ERROR);
}

static a1_route_result_t handle_remark(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_remark_t remark;
    a1_remark_feedback_result_t feedback;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_remark(body, body_length, &remark);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    feedback = a1_remark_handle_app_feedback(
        &remark, a1_gray_switch_remark_enabled(&service->gray_switch));
    return send_code(service, command, message_id, feedback.response_code,
                     feedback.response_code == A1_RESPONSE_OK
                        ? A1_ROUTE_OK : A1_ROUTE_REJECTED);
}

static int run_system_action(
    const a1_platform_t *platform,
    a1_system_action_t action)
{
    if (platform == NULL) {
        return -1;
    }
    switch (action) {
    case A1_SYSTEM_ACTION_CLEAR_RECORDINGS:
        return platform->clear_recordings == NULL
            ? -1 : platform->clear_recordings(platform->context);
    case A1_SYSTEM_ACTION_REMOVE_OTA_PACKAGE:
        return platform->remove_ota_package == NULL
            ? -1 : platform->remove_ota_package(platform->context);
    case A1_SYSTEM_ACTION_REQUEST_OTA_EVENT_9:
        return platform->request_ota_event == NULL
            ? -1 : platform->request_ota_event(platform->context, 9);
    case A1_SYSTEM_ACTION_DISABLE_LOG_RECORDING:
        return platform->set_log_recording == NULL
            ? -1 : platform->set_log_recording(platform->context, 0);
    case A1_SYSTEM_ACTION_ENABLE_LOG_RECORDING:
        return platform->set_log_recording == NULL
            ? -1 : platform->set_log_recording(platform->context, 1);
    case A1_SYSTEM_ACTION_START_LOG_RECORDING:
        return platform->start_log_recording == NULL
            ? -1 : platform->start_log_recording(platform->context);
    case A1_SYSTEM_ACTION_POWER:
        return platform->request_power_action == NULL
            ? -1 : platform->request_power_action(platform->context);
    default:
        return -1;
    }
}

static a1_route_result_t handle_system_control(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_wire_system_control_request_t request;
    a1_system_control_plan_t plan;
    const a1_platform_t *platform = service->runtime.platform;
    char json[128];
    size_t length;
    size_t index;
    int battery = 0;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_system_control(body, body_length, &request);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (request.key == A1_SYSTEM_KEY_BATTERY_PERCENT && platform != NULL &&
        platform->battery_percent != NULL) {
        battery = platform->battery_percent(platform->context);
        if (battery < 0) {
            battery = 0;
        } else if (battery > 100) {
            battery = 100;
        }
    }
    plan = a1_system_control_plan(request.key, request.has_value,
                                  request.value, (uint8_t)battery);
    for (index = 0u; index < plan.immediate_action_count; ++index) {
        if (run_system_action(platform, plan.immediate_actions[index]) != 0) {
            return send_code(service, command, message_id, A1_RESPONSE_INVALID,
                             A1_ROUTE_PLATFORM_ERROR);
        }
    }
    if (a1_wire_encode_system_control(
            json, sizeof(json), request.key, plan.response_code,
            plan.include_battery_percent, plan.battery_percent, &length) != 0 ||
        send_json(service, command, message_id, plan.response_code,
                  json, length) != 0) {
        return A1_ROUTE_PLATFORM_ERROR;
    }
    for (index = 0u; index < plan.after_response_action_count; ++index) {
        if (run_system_action(platform, plan.after_response_actions[index]) != 0) {
            return A1_ROUTE_PLATFORM_ERROR;
        }
    }
    return plan.response_code == A1_RESPONSE_OK
        ? A1_ROUTE_OK : A1_ROUTE_REJECTED;
}

static a1_route_result_t handle_gray(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    a1_wire_gray_request_t request;
    a1_gray_switch_effect_t effect;
    char json[A1_RESPONSE_BUFFER_CAPACITY];
    size_t length;
    int result;
    a1_route_result_t connected = require_connected(service, command, message_id);
    if (connected != A1_ROUTE_OK) {
        return connected;
    }
    result = a1_wire_decode_gray_switch(body, body_length, &request);
    if (result != A1_WIRE_OK) {
        return send_code(service, command, message_id, wire_error_code(result),
                         A1_ROUTE_INVALID_ARGUMENT);
    }
    if (request.action == A1_WIRE_GRAY_SET) {
        a1_gray_switch_state_t previous_state = service->gray_switch;
        a1_gray_switch_apply(&service->gray_switch, &request.update, &effect);
        if (service->persistence != NULL &&
            a1_persistence_store_gray_update(service->persistence,
                                             &request.update,
                                             &service->gray_switch) != 0) {
            service->gray_switch = previous_state;
            return send_code(service, command, message_id,
                             A1_RESPONSE_INTERNAL,
                             A1_ROUTE_PLATFORM_ERROR);
        }
        return send_code(service, command, message_id, A1_RESPONSE_OK,
                         A1_ROUTE_OK);
    }
    if (request.action == A1_WIRE_GRAY_CONTROL) {
        /* The stock path can execute arbitrary shell text. The compatible core
         * exposes its plan separately but never executes it here. */
        return send_code(service, command, message_id, A1_RESPONSE_INVALID,
                         A1_ROUTE_REJECTED);
    }
    if (a1_wire_encode_gray_switch(json, sizeof(json),
                                   &service->gray_switch, &length) != 0 ||
        send_json(service, command, message_id, A1_RESPONSE_OK,
                  json, length) != 0) {
        return A1_ROUTE_PLATFORM_ERROR;
    }
    return A1_ROUTE_OK;
}

void a1_protocol_service_init(
    a1_protocol_service_t *service,
    const a1_platform_t *platform,
    const uint8_t *device_secret,
    size_t device_secret_length)
{
    if (service == NULL) {
        return;
    }
    memset(service, 0, sizeof(*service));
    a1_runtime_init(&service->runtime, platform);
    a1_session_init(&service->session, device_secret, device_secret_length);
    a1_audio_settings_init(&service->audio_settings);
    a1_wifi_init(&service->wifi);
    a1_gray_switch_init(&service->gray_switch);
    a1_schedule_init(&service->schedule);
    a1_remark_tracker_init(&service->remark_tracker);
    a1_device_identity_init(&service->identity);
    a1_device_status_init(&service->status);
    a1_inbound_transfer_init(&service->inbound_transfer);
}

int a1_protocol_service_attach_persistence(
    a1_protocol_service_t *service,
    const a1_persistence_ops_t *persistence)
{
    a1_audio_settings_t audio_settings;
    a1_gray_switch_state_t gray_switch;
    a1_schedule_t schedule;
    if (service == NULL || persistence == NULL) {
        return -1;
    }
    if (a1_persistence_load_audio(persistence, &audio_settings) != 0 ||
        a1_persistence_load_gray(persistence, &gray_switch) != 0 ||
        a1_persistence_load_schedule(persistence, &schedule) != 0) {
        return -2;
    }
    service->audio_settings = audio_settings;
    service->gray_switch = gray_switch;
    service->schedule = schedule;
    service->persistence = persistence;
    return 0;
}

a1_route_result_t a1_protocol_service_handle(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    if (service == NULL || (body == NULL && body_length != 0u)) {
        return A1_ROUTE_INVALID_ARGUMENT;
    }
    if (a1_command_find(command) == NULL) {
        return send_code(service, command, message_id, A1_RESPONSE_OK,
                         A1_ROUTE_UNSUPPORTED);
    }
    switch (command) {
    case A1_CMD_AUTH_RANDOM:
        return handle_random(service, command, message_id, body, body_length);
    case A1_CMD_CONNECT_DEVICE:
        return handle_connect(service, command, message_id, body, body_length);
    case A1_CMD_DISCONNECT_DEVICE:
        return handle_disconnect(service, command, message_id, body, body_length);
    case A1_CMD_AUDIO:
        return handle_audio(service, command, message_id, body, body_length);
    case A1_CMD_VOICEPRINT:
        return handle_voiceprint(service, command, message_id, body, body_length);
    case A1_CMD_OPEN_AP:
        return handle_open_ap(service, command, message_id, body, body_length);
    case A1_CMD_CLOSE_AP:
        return handle_close_ap(service, command, message_id, body, body_length);
    case A1_CMD_DEVICE_INFO:
        return handle_device_info(service, command, message_id, body, body_length);
    case A1_CMD_AUDIO_STATUS:
        return handle_audio_status(service, command, message_id, body, body_length);
    case A1_CMD_FIRMWARE_VERSION:
        return handle_firmware_query(service, command, message_id,
                                     body, body_length);
    case A1_CMD_FILE_HEADER:
        return handle_inbound_header(service, command, message_id,
                                     body, body_length);
    case A1_CMD_FILE_BLOCK:
        return handle_inbound_block(service, command, message_id,
                                    body, body_length);
    case A1_CMD_SCHEDULE_RECORDING:
        return handle_schedule(service, command, message_id, body, body_length);
    case A1_CMD_GRAY_SWITCH:
        return handle_gray(service, command, message_id, body, body_length);
    case A1_CMD_FILE_LIST:
        return handle_file_list(service, command, message_id, body, body_length);
    case A1_CMD_FILE_SYNC:
        return handle_file_sync(service, command, message_id, body, body_length);
    case A1_CMD_FILE_SYNC_CANCEL:
        return handle_file_cancel(service, command, message_id, body, body_length);
    case A1_CMD_FILE_DELETE:
        return handle_file_delete(service, command, message_id, body, body_length);
    case A1_CMD_REMARK:
        return handle_remark(service, command, message_id, body, body_length);
    case A1_CMD_SYSTEM_CONTROL:
        return handle_system_control(service, command, message_id, body, body_length);
    case A1_CMD_RAW_TRANSFER:
        return handle_raw_transfer(service, command, message_id, body, body_length);
    case A1_CMD_RAW_TRANSFER_CANCEL:
        return handle_raw_cancel(service, command, message_id, body, body_length);
    default:
        return A1_ROUTE_UNSUPPORTED;
    }
}
