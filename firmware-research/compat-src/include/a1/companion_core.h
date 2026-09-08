#ifndef A1_COMPANION_CORE_H
#define A1_COMPANION_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    A1_COMPANION_CORE_COUNT = 3,
    A1_RPTUN_RESOURCE_SIZE = 0x00d0,
    A1_RPTUN_RESOURCE_ID_OFFSET = 0x0094,
    A1_RPTUN_VRING_COUNT = 8,
    A1_RPTUN_VRING_ALIGNMENT = 8,
    A1_RPTUN_BUFFER_SIZE = 512,
    A1_RPTUN_LOCAL_ARENA_SIZE = 0x10e0
};

typedef enum {
    A1_COMPANION_DSPC0 = 0,
    A1_COMPANION_BTHC0,
    A1_COMPANION_M55C1
} a1_companion_core_id_t;

/*
 * Function addresses from the stock V1.6.88 HAL_RMT_IPC_CFG_T.  They are
 * retained as reverse-engineering evidence, not callable host pointers.
 */
typedef struct {
    uint32_t irq_init_address;
    uint32_t peer_tx_irq_set_address;
    uint32_t local_tx_irq_clear_address;
    uint32_t rx_done_address;
    uint32_t irq_active_address;
    uint32_t rx_irq_suspend_address;
    uint32_t rx_irq_resume_address;
    uint32_t rx_irq_entry_address;
    uint32_t tx_irq_entry_address;
} a1_rmt_ipc_stock_ops_t;

/*
 * Read-only V1.6.88 AP-side manifest. Addresses describe the stock image and
 * are evidence for a BES/NuttX port; portable code must not dereference them.
 */
typedef struct {
    a1_companion_core_id_t id;
    const char *rpmsg_remote_name;
    const char *transport_name;
    uint32_t image_descriptor_address;
    uint32_t boot_function_address;
    uint32_t rptun_ops_address;
    uint32_t rptun_state_address;
    uint32_t ap_local_arena_address;
    uint32_t resource_table_address;
    uint32_t rmt_ipc_config_address;
    uint32_t peer_config_pointer_address;
    uint32_t local_config_pointer_address;
    uint8_t rmt_ipc_core;
    uint8_t rmt_ipc_channel;
    uint8_t resource_id;
    uint8_t channel_count;
    uint8_t channel0_message_slots;
    uint8_t rx_irq_line;
    uint8_t tx_irq_line;
    a1_rmt_ipc_stock_ops_t stock_ops;
    bool wake_lock_enabled;
    bool ap_is_master;
    bool autostart;
} a1_companion_core_t;

size_t a1_companion_core_count(void);
const a1_companion_core_t *a1_companion_core_at(size_t index);
const a1_companion_core_t *a1_companion_core_find(
    a1_companion_core_id_t id);

/* Mirrors the vring plus RPMsg-buffer arena sizing used by V1.6.88. */
size_t a1_rptun_local_arena_size(uint32_t vring_count,
                                 uint32_t buffer_size,
                                 uint32_t alignment);

/* Mirrors the stock 0xd0-byte resource scan and +0x94 channel selector. */
const uint8_t *a1_rptun_resource_find(const uint8_t *records,
                                      size_t length,
                                      uint32_t resource_id);

int a1_companion_manifest_validate(void);

#endif
