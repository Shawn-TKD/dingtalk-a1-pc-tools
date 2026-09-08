#ifndef A1_BLE_ENDPOINT_H
#define A1_BLE_ENDPOINT_H

#include <stddef.h>
#include <stdint.h>

#include "a1/frame.h"
#include "a1/protocol_service.h"

#define A1_BLE_ENDPOINT_BUFFER_SIZE (A1_FRAME_HEADER_SIZE + A1_RX_BODY_LIMIT)

typedef enum {
    A1_BLE_ENDPOINT_OK = 0,
    A1_BLE_ENDPOINT_NEED_MORE = 1,
    A1_BLE_ENDPOINT_INVALID_ARGUMENT = -1,
    A1_BLE_ENDPOINT_INVALID_KIND = -2,
    A1_BLE_ENDPOINT_PACKET_TOO_LARGE = -3,
    A1_BLE_ENDPOINT_BUFFER_FULL = -4,
    A1_BLE_ENDPOINT_PLATFORM_ERROR = -5,
    A1_BLE_ENDPOINT_FAULTED = -6
} a1_ble_endpoint_result_t;

typedef struct {
    a1_protocol_service_t *service;
    uint8_t buffer[A1_BLE_ENDPOINT_BUFFER_SIZE];
    size_t length;
    int faulted;
} a1_ble_endpoint_t;

void a1_ble_endpoint_init(
    a1_ble_endpoint_t *endpoint,
    a1_protocol_service_t *service);
void a1_ble_endpoint_reset(a1_ble_endpoint_t *endpoint);

/* Accepts arbitrary characteristic-write chunks. It buffers an incomplete
 * request and can dispatch multiple coalesced requests in one call. */
a1_ble_endpoint_result_t a1_ble_endpoint_feed(
    a1_ble_endpoint_t *endpoint,
    const uint8_t *bytes,
    size_t length,
    size_t *handled_frames);

#endif
