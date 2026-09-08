#ifndef A1_PROTOCOL_SERVICE_H
#define A1_PROTOCOL_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "a1/audio.h"
#include "a1/device_info.h"
#include "a1/firmware_update.h"
#include "a1/gray_switch.h"
#include "a1/inbound_transfer.h"
#include "a1/persistence.h"
#include "a1/remark.h"
#include "a1/router.h"
#include "a1/runtime.h"
#include "a1/schedule.h"
#include "a1/session.h"
#include "a1/wifi.h"
#include "a1/wire_responses.h"

typedef struct {
    a1_runtime_t runtime;
    a1_session_t session;
    a1_audio_settings_t audio_settings;
    a1_wifi_state_t wifi;
    a1_gray_switch_state_t gray_switch;
    a1_schedule_t schedule;
    a1_remark_tracker_t remark_tracker;
    a1_device_identity_t identity;
    a1_device_status_t status;
    a1_wire_capabilities_t capabilities;
    const a1_persistence_ops_t *persistence;
    bool firmware_query_received;
    char firmware_target_version[A1_UPDATE_VERSION_CAPACITY];
    uint32_t firmware_resume_offset;
    a1_inbound_transfer_t inbound_transfer;
} a1_protocol_service_t;

void a1_protocol_service_init(
    a1_protocol_service_t *service,
    const a1_platform_t *platform,
    const uint8_t *device_secret,
    size_t device_secret_length);

/* Attaches an optional storage backend and loads all persisted state. The
 * service is left unchanged if any stored object cannot be read or decoded. */
int a1_protocol_service_attach_persistence(
    a1_protocol_service_t *service,
    const a1_persistence_ops_t *persistence);

a1_route_result_t a1_protocol_service_handle(
    a1_protocol_service_t *service,
    uint16_t command,
    uint8_t message_id,
    const uint8_t *body,
    size_t body_length);

#endif
