#include "a1/router.h"

#include "a1/runtime.h"

#define A1_CODE_OK 200u

static a1_route_result_t unsupported_handler(
    a1_runtime_t *runtime,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    (void)runtime;
    (void)message_id;
    (void)body;
    (void)body_length;
    return A1_ROUTE_UNSUPPORTED;
}

/*
 * Handler bodies remain deliberately unimplemented until their JSON fields and
 * side effects have been traced. The registry itself is recovered from the
 * dtiot_ble_recv_data switch at AP 0x103ce7b8.
 */
static const a1_command_descriptor_t COMMANDS[] = {
    {A1_CMD_DEVICE_INFO, "device_info", A1_RISK_READ_ONLY, unsupported_handler},
    {A1_CMD_RESET_DEVICE, "reset_device", A1_RISK_DESTRUCTIVE, unsupported_handler},
    {A1_CMD_ACTIVE_INFO, "active_info", A1_RISK_READ_ONLY, unsupported_handler},
    {A1_CMD_ACTIVE_DEVICE, "active_device", A1_RISK_MUTATING, unsupported_handler},
    {A1_CMD_AUTH_RANDOM, "auth_random", A1_RISK_READ_ONLY, unsupported_handler},
    {A1_CMD_AUTH, "auth", A1_RISK_READ_ONLY, unsupported_handler},
    {A1_CMD_AUDIO, "audio", A1_RISK_MUTATING, unsupported_handler},
    {A1_CMD_VOICEPRINT, "voiceprint", A1_RISK_MUTATING, unsupported_handler},
    {A1_CMD_REMARK, "remark", A1_RISK_MUTATING, unsupported_handler},
    {A1_CMD_FILE_LIST, "file_list", A1_RISK_READ_ONLY, unsupported_handler},
    {A1_CMD_FILE_SYNC, "file_sync", A1_RISK_READ_ONLY, unsupported_handler},
    {A1_CMD_FILE_SYNC_CANCEL, "file_sync_cancel", A1_RISK_READ_ONLY, unsupported_handler},
    {A1_CMD_FILE_DELETE, "file_delete", A1_RISK_DESTRUCTIVE, unsupported_handler},
    {A1_CMD_FILE_HEADER, "file_header", A1_RISK_MUTATING, unsupported_handler},
    {A1_CMD_FILE_BLOCK, "file_block", A1_RISK_MUTATING, unsupported_handler},
    {A1_CMD_SCHEDULE_RECORDING, "schedule_recording", A1_RISK_MUTATING, unsupported_handler},
    {A1_CMD_OPEN_AP, "open_ap", A1_RISK_MUTATING, unsupported_handler},
    {A1_CMD_CLOSE_AP, "close_ap", A1_RISK_MUTATING, unsupported_handler},
    {A1_CMD_TRANSFER_INFO, "transfer_info", A1_RISK_SENSITIVE, unsupported_handler},
    {A1_CMD_AUDIO_STATUS, "audio_status", A1_RISK_READ_ONLY, unsupported_handler},
    {A1_CMD_CONNECT_DEVICE, "connect_device", A1_RISK_MUTATING, unsupported_handler},
    {A1_CMD_DISCONNECT_DEVICE, "disconnect_device", A1_RISK_MUTATING, unsupported_handler},
    {A1_CMD_FIRMWARE_VERSION, "firmware_version", A1_RISK_READ_ONLY, unsupported_handler},
    {A1_CMD_SYSTEM_CONTROL, "system_control", A1_RISK_DESTRUCTIVE, unsupported_handler},
    {A1_CMD_GRAY_SWITCH, "gray_switch", A1_RISK_MUTATING, unsupported_handler},
    {A1_CMD_RAW_TRANSFER, "raw_transfer", A1_RISK_READ_ONLY, unsupported_handler},
    {A1_CMD_RAW_TRANSFER_CANCEL, "raw_transfer_cancel", A1_RISK_READ_ONLY, unsupported_handler}
};

const a1_command_descriptor_t *a1_command_registry(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(COMMANDS) / sizeof(COMMANDS[0]);
    }
    return COMMANDS;
}

const a1_command_descriptor_t *a1_command_find(uint16_t command)
{
    size_t count;
    size_t index;

    a1_command_registry(&count);
    for (index = 0; index < count; ++index) {
        if (COMMANDS[index].command == command) {
            return &COMMANDS[index];
        }
    }
    return NULL;
}

a1_route_result_t a1_route_request(
    a1_runtime_t *runtime,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length)
{
    const a1_command_descriptor_t *descriptor;

    if (runtime == NULL || (body == NULL && body_length != 0)) {
        return A1_ROUTE_INVALID_ARGUMENT;
    }
    descriptor = a1_command_find(command);
    if (descriptor == NULL) {
        /* Stock V1.6.88 responds code=200 to unknown commands. */
        if (runtime->platform != NULL && runtime->platform->send_response != NULL) {
            runtime->platform->send_response(
                runtime->platform->context,
                command,
                message_id,
                A1_CODE_OK,
                NULL);
        }
        return A1_ROUTE_UNSUPPORTED;
    }
    return descriptor->handler(runtime, message_id, body, body_length);
}
