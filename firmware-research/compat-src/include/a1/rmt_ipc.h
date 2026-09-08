#ifndef A1_RMT_IPC_H
#define A1_RMT_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    A1_RMT_IPC_IRQ_SEND_IND = 0,
    A1_RMT_IPC_IRQ_RECV_DONE = 1
} a1_rmt_ipc_irq_type_t;

typedef struct a1_rmt_ipc_message {
    struct a1_rmt_ipc_message *next;
    size_t length;
    const uint8_t *data;
} a1_rmt_ipc_message_t;

typedef struct {
    a1_rmt_ipc_message_t message;
    bool in_use;
} a1_rmt_ipc_send_slot_t;

typedef size_t (*a1_rmt_ipc_rx_handler_t)(void *context,
                                          const uint8_t *data,
                                          size_t length);
typedef void (*a1_rmt_ipc_tx_handler_t)(void *context,
                                        const uint8_t *data,
                                        size_t length);

/*
 * Board-facing operations.  The first seven callbacks correspond to the
 * recovered V1.6.88 HAL_RMT_IPC_CFG_T function slots.  IRQ enable and memory
 * barrier are explicit here because the stock generic layer performs those
 * operations directly through CMSIS.
 */
typedef struct {
    void *context;
    void (*irq_init)(void *context, uint8_t channel);
    void (*peer_tx_irq_set)(void *context, uint8_t channel);
    void (*local_tx_irq_clear)(void *context, uint8_t channel);
    void (*rx_done)(void *context, uint8_t channel);
    bool (*irq_active)(void *context,
                       uint8_t channel,
                       a1_rmt_ipc_irq_type_t type);
    void (*rx_irq_suspend)(void *context, uint8_t channel);
    void (*rx_irq_resume)(void *context, uint8_t channel);
    void (*rx_irq_enable)(void *context, uint8_t channel, bool enabled);
    void (*tx_irq_enable)(void *context, uint8_t channel, bool enabled);
    void (*memory_barrier)(void *context);
    void (*busy_changed)(void *context, uint8_t channel, bool busy);
} a1_rmt_ipc_port_t;

typedef struct {
    const a1_rmt_ipc_port_t *port;
    a1_rmt_ipc_send_slot_t *send_slots;
    size_t send_slot_count;
    a1_rmt_ipc_message_t *active_send;
    a1_rmt_ipc_message_t *pending_send;
    a1_rmt_ipc_message_t receive_pending;
    a1_rmt_ipc_rx_handler_t receive_handler;
    a1_rmt_ipc_tx_handler_t transmit_handler;
    void *handler_context;
    uint8_t channel;
    bool opened;
    bool receive_enabled;
    bool manual_rx_done;
    bool busy;
} a1_rmt_ipc_channel_t;

int a1_rmt_ipc_channel_init(a1_rmt_ipc_channel_t *ipc,
                            const a1_rmt_ipc_port_t *port,
                            uint8_t channel,
                            a1_rmt_ipc_send_slot_t *send_slots,
                            size_t send_slot_count);
int a1_rmt_ipc_open(a1_rmt_ipc_channel_t *ipc,
                    a1_rmt_ipc_rx_handler_t receive_handler,
                    a1_rmt_ipc_tx_handler_t transmit_handler,
                    void *handler_context,
                    bool manual_rx_done);
void a1_rmt_ipc_close(a1_rmt_ipc_channel_t *ipc);
int a1_rmt_ipc_start_receive(a1_rmt_ipc_channel_t *ipc);
int a1_rmt_ipc_stop_receive(a1_rmt_ipc_channel_t *ipc);

/* Data is borrowed until its TX-complete callback. Returns the slot index. */
int a1_rmt_ipc_send(a1_rmt_ipc_channel_t *ipc,
                    const uint8_t *data,
                    size_t length);
bool a1_rmt_ipc_tx_active(const a1_rmt_ipc_channel_t *ipc,
                          size_t slot_index);

/* Called by board ISR wrappers after resolving the peer's shared list. */
int a1_rmt_ipc_handle_rx_irq(a1_rmt_ipc_channel_t *ipc,
                             const a1_rmt_ipc_message_t *peer_messages);
int a1_rmt_ipc_handle_tx_irq(a1_rmt_ipc_channel_t *ipc);
int a1_rmt_ipc_receive_done(a1_rmt_ipc_channel_t *ipc);

#endif
