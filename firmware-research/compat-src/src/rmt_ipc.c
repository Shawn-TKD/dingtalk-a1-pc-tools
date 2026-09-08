#include "a1/rmt_ipc.h"

#include <string.h>

static int port_is_complete(const a1_rmt_ipc_port_t *port)
{
    return port != NULL && port->irq_init != NULL &&
           port->peer_tx_irq_set != NULL &&
           port->local_tx_irq_clear != NULL && port->rx_done != NULL &&
           port->irq_active != NULL && port->rx_irq_suspend != NULL &&
           port->rx_irq_resume != NULL && port->rx_irq_enable != NULL &&
           port->tx_irq_enable != NULL && port->memory_barrier != NULL;
}

static void set_busy(a1_rmt_ipc_channel_t *ipc, bool busy)
{
    if (ipc->busy == busy) {
        return;
    }
    ipc->busy = busy;
    if (ipc->port->busy_changed != NULL) {
        ipc->port->busy_changed(ipc->port->context, ipc->channel, busy);
    }
}

static a1_rmt_ipc_send_slot_t *slot_from_message(
    a1_rmt_ipc_channel_t *ipc,
    a1_rmt_ipc_message_t *message)
{
    size_t index;
    for (index = 0u; index < ipc->send_slot_count; ++index) {
        if (&ipc->send_slots[index].message == message) {
            return &ipc->send_slots[index];
        }
    }
    return NULL;
}

int a1_rmt_ipc_channel_init(a1_rmt_ipc_channel_t *ipc,
                            const a1_rmt_ipc_port_t *port,
                            uint8_t channel,
                            a1_rmt_ipc_send_slot_t *send_slots,
                            size_t send_slot_count)
{
    if (ipc == NULL || !port_is_complete(port) || send_slots == NULL ||
        send_slot_count == 0u) {
        return -1;
    }
    memset(ipc, 0, sizeof(*ipc));
    memset(send_slots, 0, send_slot_count * sizeof(*send_slots));
    ipc->port = port;
    ipc->channel = channel;
    ipc->send_slots = send_slots;
    ipc->send_slot_count = send_slot_count;
    return 0;
}

int a1_rmt_ipc_open(a1_rmt_ipc_channel_t *ipc,
                    a1_rmt_ipc_rx_handler_t receive_handler,
                    a1_rmt_ipc_tx_handler_t transmit_handler,
                    void *handler_context,
                    bool manual_rx_done)
{
    if (ipc == NULL || ipc->port == NULL || receive_handler == NULL) {
        return -1;
    }
    if (ipc->opened) {
        return 1;
    }
    ipc->port->rx_irq_enable(ipc->port->context, ipc->channel, false);
    ipc->port->tx_irq_enable(ipc->port->context, ipc->channel, false);
    ipc->port->irq_init(ipc->port->context, ipc->channel);
    memset(ipc->send_slots, 0,
           ipc->send_slot_count * sizeof(*ipc->send_slots));
    memset(&ipc->receive_pending, 0, sizeof(ipc->receive_pending));
    ipc->active_send = NULL;
    ipc->pending_send = NULL;
    ipc->receive_handler = receive_handler;
    ipc->transmit_handler = transmit_handler;
    ipc->handler_context = handler_context;
    ipc->manual_rx_done = manual_rx_done;
    ipc->receive_enabled = false;
    ipc->opened = true;
    ipc->port->tx_irq_enable(ipc->port->context, ipc->channel, true);
    return 0;
}

void a1_rmt_ipc_close(a1_rmt_ipc_channel_t *ipc)
{
    if (ipc == NULL || ipc->port == NULL || !ipc->opened) {
        return;
    }
    ipc->port->rx_irq_enable(ipc->port->context, ipc->channel, false);
    ipc->port->tx_irq_enable(ipc->port->context, ipc->channel, false);
    ipc->port->irq_init(ipc->port->context, ipc->channel);
    set_busy(ipc, false);
    memset(ipc->send_slots, 0,
           ipc->send_slot_count * sizeof(*ipc->send_slots));
    ipc->active_send = NULL;
    ipc->pending_send = NULL;
    memset(&ipc->receive_pending, 0, sizeof(ipc->receive_pending));
    ipc->receive_handler = NULL;
    ipc->transmit_handler = NULL;
    ipc->handler_context = NULL;
    ipc->receive_enabled = false;
    ipc->manual_rx_done = false;
    ipc->opened = false;
}

int a1_rmt_ipc_start_receive(a1_rmt_ipc_channel_t *ipc)
{
    if (ipc == NULL || !ipc->opened) {
        return -1;
    }
    ipc->receive_enabled = true;
    ipc->port->rx_irq_enable(ipc->port->context, ipc->channel, true);
    if (ipc->receive_pending.data != NULL) {
        ipc->port->rx_irq_resume(ipc->port->context, ipc->channel);
    }
    return 0;
}

int a1_rmt_ipc_stop_receive(a1_rmt_ipc_channel_t *ipc)
{
    if (ipc == NULL || !ipc->opened) {
        return -1;
    }
    ipc->port->rx_irq_enable(ipc->port->context, ipc->channel, false);
    ipc->receive_enabled = false;
    return 0;
}

int a1_rmt_ipc_send(a1_rmt_ipc_channel_t *ipc,
                    const uint8_t *data,
                    size_t length)
{
    a1_rmt_ipc_message_t *tail;
    size_t index;
    if (ipc == NULL || !ipc->opened || data == NULL || length == 0u) {
        return -1;
    }
    for (index = 0u; index < ipc->send_slot_count; ++index) {
        if (!ipc->send_slots[index].in_use) {
            break;
        }
    }
    if (index == ipc->send_slot_count) {
        return -2;
    }

    ipc->send_slots[index].in_use = true;
    ipc->send_slots[index].message.next = NULL;
    ipc->send_slots[index].message.length = length;
    ipc->send_slots[index].message.data = data;

    if (ipc->active_send == NULL) {
        ipc->active_send = &ipc->send_slots[index].message;
        ipc->port->memory_barrier(ipc->port->context);
        ipc->port->peer_tx_irq_set(ipc->port->context, ipc->channel);
    } else if (ipc->pending_send == NULL) {
        ipc->pending_send = &ipc->send_slots[index].message;
    } else {
        tail = ipc->pending_send;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = &ipc->send_slots[index].message;
    }
    set_busy(ipc, true);
    return (int)index;
}

bool a1_rmt_ipc_tx_active(const a1_rmt_ipc_channel_t *ipc,
                          size_t slot_index)
{
    return ipc != NULL && slot_index < ipc->send_slot_count &&
           ipc->send_slots[slot_index].in_use;
}

int a1_rmt_ipc_handle_tx_irq(a1_rmt_ipc_channel_t *ipc)
{
    a1_rmt_ipc_message_t *message;
    int completed = 0;
    if (ipc == NULL || !ipc->opened ||
        !ipc->port->irq_active(ipc->port->context, ipc->channel,
                              A1_RMT_IPC_IRQ_RECV_DONE)) {
        return 0;
    }
    ipc->port->local_tx_irq_clear(ipc->port->context, ipc->channel);
    message = ipc->active_send;
    while (message != NULL) {
        a1_rmt_ipc_message_t *next = message->next;
        a1_rmt_ipc_send_slot_t *slot = slot_from_message(ipc, message);
        if (ipc->transmit_handler != NULL) {
            ipc->transmit_handler(ipc->handler_context,
                                  message->data,
                                  message->length);
        }
        if (slot != NULL) {
            memset(slot, 0, sizeof(*slot));
        }
        message = next;
        ++completed;
    }
    ipc->active_send = ipc->pending_send;
    ipc->pending_send = NULL;
    if (ipc->active_send != NULL) {
        ipc->port->memory_barrier(ipc->port->context);
        ipc->port->peer_tx_irq_set(ipc->port->context, ipc->channel);
    } else {
        set_busy(ipc, false);
    }
    return completed;
}

int a1_rmt_ipc_handle_rx_irq(a1_rmt_ipc_channel_t *ipc,
                             const a1_rmt_ipc_message_t *peer_messages)
{
    const a1_rmt_ipc_message_t *message;
    if (ipc == NULL || !ipc->opened || !ipc->receive_enabled ||
        !ipc->port->irq_active(ipc->port->context, ipc->channel,
                              A1_RMT_IPC_IRQ_SEND_IND)) {
        return 0;
    }
    ipc->port->rx_irq_suspend(ipc->port->context, ipc->channel);
    message = ipc->receive_pending.data != NULL ? &ipc->receive_pending
                                                : peer_messages;
    while (message != NULL) {
        size_t consumed = ipc->receive_handler(ipc->handler_context,
                                               message->data,
                                               message->length);
        if (consumed > message->length) {
            return -1;
        }
        if (consumed < message->length) {
            ipc->receive_pending.next = message->next;
            ipc->receive_pending.data = message->data + consumed;
            ipc->receive_pending.length = message->length - consumed;
            return 1;
        }
        message = message->next;
    }
    memset(&ipc->receive_pending, 0, sizeof(ipc->receive_pending));
    if (!ipc->manual_rx_done) {
        ipc->port->rx_done(ipc->port->context, ipc->channel);
    }
    return 1;
}

int a1_rmt_ipc_receive_done(a1_rmt_ipc_channel_t *ipc)
{
    if (ipc == NULL || !ipc->opened || ipc->receive_pending.data != NULL) {
        return -1;
    }
    ipc->port->rx_done(ipc->port->context, ipc->channel);
    return 0;
}
