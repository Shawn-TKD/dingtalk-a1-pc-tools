#include "a1/ble_endpoint.h"

#include <string.h>

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

void a1_ble_endpoint_init(
    a1_ble_endpoint_t *endpoint,
    a1_protocol_service_t *service)
{
    if (endpoint != NULL) {
        memset(endpoint, 0, sizeof(*endpoint));
        endpoint->service = service;
    }
}

void a1_ble_endpoint_reset(a1_ble_endpoint_t *endpoint)
{
    a1_protocol_service_t *service;
    if (endpoint == NULL) {
        return;
    }
    service = endpoint->service;
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->service = service;
}

a1_ble_endpoint_result_t a1_ble_endpoint_feed(
    a1_ble_endpoint_t *endpoint,
    const uint8_t *bytes,
    size_t length,
    size_t *handled_frames)
{
    if (endpoint == NULL || endpoint->service == NULL ||
        handled_frames == NULL || (bytes == NULL && length != 0u)) {
        return A1_BLE_ENDPOINT_INVALID_ARGUMENT;
    }
    *handled_frames = 0u;
    if (endpoint->faulted) {
        return A1_BLE_ENDPOINT_FAULTED;
    }
    if (length > sizeof(endpoint->buffer) - endpoint->length) {
        return A1_BLE_ENDPOINT_BUFFER_FULL;
    }
    if (length != 0u) {
        memcpy(endpoint->buffer + endpoint->length, bytes, length);
        endpoint->length += length;
    }

    while (endpoint->length >= A1_FRAME_HEADER_SIZE) {
        uint32_t body_length;
        size_t frame_length;
        a1_frame_view_t frame;
        size_t consumed;
        a1_frame_result_t decode_result;
        a1_route_result_t route_result;

        if (endpoint->buffer[0] != A1_FRAME_REQUEST) {
            endpoint->faulted = 1;
            return A1_BLE_ENDPOINT_INVALID_KIND;
        }
        body_length = read_be32(endpoint->buffer + 4u);
        if (body_length > A1_RX_BODY_LIMIT) {
            endpoint->faulted = 1;
            return A1_BLE_ENDPOINT_PACKET_TOO_LARGE;
        }
        frame_length = A1_FRAME_HEADER_SIZE + (size_t)body_length;
        if (endpoint->length < frame_length) {
            break;
        }
        decode_result = a1_frame_decode(endpoint->buffer, frame_length,
                                        &frame, &consumed);
        if (decode_result != A1_FRAME_OK || consumed != frame_length) {
            endpoint->faulted = 1;
            return A1_BLE_ENDPOINT_PACKET_TOO_LARGE;
        }
        route_result = a1_protocol_service_handle(
            endpoint->service, frame.command, frame.message_id,
            frame.body, frame.body_length);
        if (route_result == A1_ROUTE_PLATFORM_ERROR) {
            return A1_BLE_ENDPOINT_PLATFORM_ERROR;
        }
        endpoint->length -= frame_length;
        if (endpoint->length != 0u) {
            memmove(endpoint->buffer,
                    endpoint->buffer + frame_length,
                    endpoint->length);
        }
        *handled_frames += 1u;
    }
    return *handled_frames == 0u
        ? A1_BLE_ENDPOINT_NEED_MORE
        : A1_BLE_ENDPOINT_OK;
}
