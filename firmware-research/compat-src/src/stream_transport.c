#include "a1/stream_transport.h"

#include <string.h>

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static uint64_t read_be64(const uint8_t *bytes)
{
    return ((uint64_t)read_be32(bytes) << 32) | read_be32(bytes + 4u);
}

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void write_be64(uint8_t *bytes, uint64_t value)
{
    write_be32(bytes, (uint32_t)(value >> 32));
    write_be32(bytes + 4u, (uint32_t)value);
}

static a1_stream_fragment_t *queue_entry(
    a1_stream_queue_t *queue,
    size_t ordinal)
{
    return &queue->entries[(queue->head + ordinal) % A1_STREAM_QUEUE_LIMIT];
}

static void queue_drop_oldest(a1_stream_queue_t *queue)
{
    queue->head = (queue->head + 1u) % A1_STREAM_QUEUE_LIMIT;
    queue->count -= 1u;
    if (queue->dropped_frames < A1_STREAM_DROP_COUNTER_LIMIT) {
        queue->dropped_frames += 1u;
    }
}

void a1_stream_queue_init(a1_stream_queue_t *queue, uint64_t fid)
{
    if (queue != NULL) {
        memset(queue, 0, sizeof(*queue));
        queue->fid = fid;
    }
}

int a1_stream_queue_push(
    a1_stream_queue_t *queue,
    uint64_t fid,
    uint64_t timestamp,
    uint8_t type,
    const uint8_t *data,
    size_t data_length)
{
    a1_stream_fragment_t *entry;

    if (queue == NULL || (data == NULL && data_length != 0u) ||
        data_length > A1_STREAM_FRAGMENT_DATA_LIMIT || type > 7u) {
        return -1;
    }
    if (queue->count != 0u && fid != queue->fid) {
        return -2;
    }
    queue->fid = fid;
    if (queue->count == A1_STREAM_QUEUE_LIMIT) {
        queue_drop_oldest(queue);
    }

    entry = queue_entry(queue, queue->count);
    memset(entry, 0, sizeof(*entry));
    entry->fid = fid;
    entry->sequence = ++queue->next_sequence;
    entry->timestamp = timestamp;
    entry->type = type;
    entry->data_length = (uint16_t)data_length;
    if (data_length != 0u) {
        memcpy(entry->data, data, data_length);
    }
    queue->count += 1u;
    queue->received_bytes += (uint32_t)data_length;
    return 0;
}

int a1_stream_type_flushes_batch(uint8_t type)
{
    return type == 1u || type == 3u || type == 5u;
}

int a1_stream_queue_next_wire(
    a1_stream_queue_t *queue,
    uint8_t *output,
    size_t output_capacity,
    size_t *written)
{
    a1_stream_fragment_t *entry;
    uint64_t timestamp = 0u;
    uint32_t sequence = 0u;
    uint8_t type = 0u;
    uint8_t frame_count = 0u;
    size_t data_length = 0u;
    size_t index;
    size_t copied = 0u;
    size_t required_length;
    size_t trailer_offset;
    uint32_t dropped;

    if (queue == NULL || output == NULL || written == NULL) {
        return -1;
    }
    if (queue->count == 0u) {
        *written = 0u;
        return 1;
    }

    while ((size_t)frame_count < queue->count &&
           frame_count < A1_STREAM_QUEUE_LIMIT) {
        entry = queue_entry(queue, frame_count);
        if (data_length != 0u &&
            data_length + entry->data_length > A1_STREAM_BATCH_DATA_LIMIT) {
            break;
        }
        data_length += entry->data_length;
        timestamp = entry->timestamp;
        sequence = entry->sequence;
        type = entry->type;
        frame_count += 1u;
        if (a1_stream_type_flushes_batch(type)) {
            break;
        }
    }

    required_length = A1_STREAM_WIRE_HEADER_SIZE + data_length +
        A1_STREAM_WIRE_TRAILER_SIZE;
    if (output_capacity < required_length) {
        return -2;
    }
    write_be64(output, queue->fid);
    write_be64(output + 8u, timestamp);
    write_be32(output + 16u, sequence);
    write_be32(output + 20u, (uint32_t)data_length);
    for (index = 0u; index < frame_count; ++index) {
        entry = queue_entry(queue, index);
        if (entry->data_length != 0u) {
            memcpy(output + A1_STREAM_WIRE_HEADER_SIZE + copied,
                   entry->data,
                   entry->data_length);
        }
        copied += entry->data_length;
    }
    trailer_offset = A1_STREAM_WIRE_HEADER_SIZE + data_length;
    dropped = queue->dropped_frames & A1_STREAM_DROP_COUNTER_LIMIT;
    write_be16(output + trailer_offset, (uint16_t)dropped);
    output[trailer_offset + 2u] = frame_count;
    output[trailer_offset + 3u] =
        (uint8_t)(((dropped >> 13) & 0xf8u) | (type & 0x07u));
    memcpy(output + trailer_offset + 4u, "ZZZZ", 4u);
    queue->head = (queue->head + frame_count) % A1_STREAM_QUEUE_LIMIT;
    queue->count -= frame_count;
    queue->sent_bytes += (uint32_t)data_length;
    *written = required_length;
    return 0;
}

int a1_stream_wire_decode(
    const uint8_t *body,
    size_t body_length,
    a1_stream_wire_view_t *view)
{
    uint32_t data_length;
    size_t trailer_offset;
    uint32_t dropped;

    if (body == NULL || view == NULL ||
        body_length < A1_STREAM_WIRE_HEADER_SIZE + A1_STREAM_WIRE_TRAILER_SIZE) {
        return -1;
    }
    data_length = read_be32(body + 20u);
    if (data_length > A1_STREAM_BATCH_DATA_LIMIT ||
        body_length != A1_STREAM_WIRE_HEADER_SIZE +
            (size_t)data_length + A1_STREAM_WIRE_TRAILER_SIZE) {
        return -2;
    }
    trailer_offset = A1_STREAM_WIRE_HEADER_SIZE + (size_t)data_length;
    if (memcmp(body + trailer_offset + 4u, "ZZZZ", 4u) != 0) {
        return -3;
    }
    dropped = read_be16(body + trailer_offset);
    dropped |= ((uint32_t)body[trailer_offset + 3u] & 0xf8u) << 13;

    view->fid = read_be64(body);
    view->timestamp = read_be64(body + 8u);
    view->sequence = read_be32(body + 16u);
    view->data_length = data_length;
    view->data = body + A1_STREAM_WIRE_HEADER_SIZE;
    view->dropped_frames = dropped;
    view->frame_count = body[trailer_offset + 2u];
    view->type = body[trailer_offset + 3u] & 0x07u;
    return 0;
}
