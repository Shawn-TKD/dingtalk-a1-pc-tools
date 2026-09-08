#include "a1/companion_core.h"

#include <string.h>

static const a1_companion_core_t CORES[A1_COMPANION_CORE_COUNT] = {
    {A1_COMPANION_DSPC0, "audio", "M55C0_2_DSPC0",
     UINT32_C(0x30c90000), UINT32_C(0x10232354),
     UINT32_C(0x10486488), UINT32_C(0x201d16f4),
     UINT32_C(0x201e057c), 0u, UINT32_C(0x104958b4),
     UINT32_C(0x20bbffa4), UINT32_C(0x20bbffa0),
     2u, 0u, 3u, 2u, 5u, 76u, 79u,
     {UINT32_C(0x10241d59), UINT32_C(0x10241d73),
      UINT32_C(0x10241df5), UINT32_C(0x10241d9f),
      UINT32_C(0x10241db3), UINT32_C(0x10241e11),
      UINT32_C(0x10241d87), UINT32_C(0x10241de9),
      UINT32_C(0x10241ddd)},
     false, true, false},
    {A1_COMPANION_BTHC0, "bth", "M55C0_2_BTHC0",
     UINT32_C(0x28000000), UINT32_C(0x1022c4a8),
     UINT32_C(0x104864c0), UINT32_C(0x201d1750),
     UINT32_C(0x23c00000), 0u, UINT32_C(0x1049597c),
     UINT32_C(0x22ffffe4), UINT32_C(0x22ffffe0),
     3u, 0u, 1u, 2u, 8u, 96u, 94u,
     {UINT32_C(0x10241f77), UINT32_C(0x10241faf),
      UINT32_C(0x10242087), UINT32_C(0x10242059),
      UINT32_C(0x10241fdb), UINT32_C(0x1024203d),
      UINT32_C(0x10241fc7), UINT32_C(0x10242011),
      UINT32_C(0x10242005)},
     true, true, false},
    {A1_COMPANION_M55C1, "apc1", "M55C0_2_M55C1",
     UINT32_C(0x30990000), UINT32_C(0x1023388c),
     UINT32_C(0x104864f8), UINT32_C(0x201d17ac),
     UINT32_C(0x201e1664), UINT32_C(0x23c0b000),
     UINT32_C(0x10495850), UINT32_C(0x21dbffa4),
     UINT32_C(0x21dbffa0),
     1u, 0u, 0u, 2u, 5u, 88u, 91u,
     {UINT32_C(0x10241c6d), UINT32_C(0x10241c8b),
      UINT32_C(0x10241d19), UINT32_C(0x10241cbb),
      UINT32_C(0x10241cd3), UINT32_C(0x10241d39),
      UINT32_C(0x10241ca3), UINT32_C(0x10241d0d),
      UINT32_C(0x10241d01)},
     false, true, false}
};

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

size_t a1_companion_core_count(void)
{
    return A1_COMPANION_CORE_COUNT;
}

const a1_companion_core_t *a1_companion_core_at(size_t index)
{
    return index < A1_COMPANION_CORE_COUNT ? &CORES[index] : NULL;
}

const a1_companion_core_t *a1_companion_core_find(
    a1_companion_core_id_t id)
{
    size_t index;
    for (index = 0u; index < A1_COMPANION_CORE_COUNT; ++index) {
        if (CORES[index].id == id) {
            return &CORES[index];
        }
    }
    return NULL;
}

size_t a1_rptun_local_arena_size(uint32_t vring_count,
                                 uint32_t buffer_size,
                                 uint32_t alignment)
{
    size_t descriptor_and_available;
    size_t vring_size;
    if (vring_count == 0u || alignment == 0u ||
        (alignment & (alignment - 1u)) != 0u) {
        return 0u;
    }

    descriptor_and_available =
        align_up((size_t)18u * vring_count + 6u, alignment);
    vring_size = descriptor_and_available +
                 (size_t)8u * vring_count + 6u;
    return align_up(vring_size + (size_t)buffer_size * vring_count,
                    alignment);
}

const uint8_t *a1_rptun_resource_find(const uint8_t *records,
                                      size_t length,
                                      uint32_t resource_id)
{
    size_t offset;
    if (records == NULL || length < A1_RPTUN_RESOURCE_SIZE ||
        length % A1_RPTUN_RESOURCE_SIZE != 0u) {
        return NULL;
    }
    for (offset = 0u; offset < length; offset += A1_RPTUN_RESOURCE_SIZE) {
        if (read_le32(records + offset + A1_RPTUN_RESOURCE_ID_OFFSET) ==
            resource_id) {
            return records + offset;
        }
    }
    return NULL;
}

int a1_companion_manifest_validate(void)
{
    size_t index;
    if (a1_rptun_local_arena_size(A1_RPTUN_VRING_COUNT,
                                  A1_RPTUN_BUFFER_SIZE,
                                  A1_RPTUN_VRING_ALIGNMENT) !=
        A1_RPTUN_LOCAL_ARENA_SIZE) {
        return -1;
    }
    for (index = 0u; index < A1_COMPANION_CORE_COUNT; ++index) {
        const a1_companion_core_t *core = &CORES[index];
        if (core->id != (a1_companion_core_id_t)index ||
            core->rpmsg_remote_name[0] == '\0' ||
            core->transport_name[0] == '\0' ||
            core->image_descriptor_address == 0u ||
            core->boot_function_address == 0u ||
            core->rptun_ops_address == 0u ||
            core->rptun_state_address == 0u ||
            core->ap_local_arena_address == 0u ||
            core->rmt_ipc_config_address == 0u ||
            core->peer_config_pointer_address == 0u ||
            core->local_config_pointer_address == 0u ||
            core->channel_count != 2u ||
            core->channel0_message_slots == 0u ||
            core->rx_irq_line == 0u || core->tx_irq_line == 0u ||
            core->stock_ops.irq_init_address == 0u ||
            core->stock_ops.peer_tx_irq_set_address == 0u ||
            core->stock_ops.local_tx_irq_clear_address == 0u ||
            core->stock_ops.rx_done_address == 0u ||
            core->stock_ops.irq_active_address == 0u ||
            core->stock_ops.rx_irq_suspend_address == 0u ||
            core->stock_ops.rx_irq_resume_address == 0u ||
            core->stock_ops.rx_irq_entry_address == 0u ||
            core->stock_ops.tx_irq_entry_address == 0u ||
            !core->ap_is_master || core->autostart) {
            return -2;
        }
    }
    if (strcmp(CORES[A1_COMPANION_DSPC0].rpmsg_remote_name, "audio") != 0 ||
        strcmp(CORES[A1_COMPANION_BTHC0].rpmsg_remote_name, "bth") != 0 ||
        strcmp(CORES[A1_COMPANION_M55C1].rpmsg_remote_name, "apc1") != 0) {
        return -3;
    }
    return 0;
}
