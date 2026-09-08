#ifndef A1_STREAM_TRANSPORT_H
#define A1_STREAM_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#define A1_STREAM_FRAGMENT_DATA_LIMIT 200u
#define A1_STREAM_QUEUE_LIMIT 50u
#define A1_STREAM_BATCH_DATA_LIMIT 10000u
#define A1_STREAM_WIRE_HEADER_SIZE 24u
#define A1_STREAM_WIRE_TRAILER_SIZE 8u
#define A1_STREAM_DROP_COUNTER_LIMIT 0x1fffffu

typedef struct {
    uint64_t fid;
    uint32_t sequence;
    uint64_t timestamp;
    uint8_t type;
    uint16_t data_length;
    uint8_t data[A1_STREAM_FRAGMENT_DATA_LIMIT];
} a1_stream_fragment_t;

typedef struct {
    a1_stream_fragment_t entries[A1_STREAM_QUEUE_LIMIT];
    size_t head;
    size_t count;
    uint64_t fid;
    uint32_t next_sequence;
    uint32_t received_bytes;
    uint32_t sent_bytes;
    uint32_t dropped_frames;
} a1_stream_queue_t;

typedef struct {
    uint64_t fid;
    uint64_t timestamp;
    uint32_t sequence;
    uint32_t data_length;
    const uint8_t *data;
    uint32_t dropped_frames;
    uint8_t frame_count;
    uint8_t type;
} a1_stream_wire_view_t;

void a1_stream_queue_init(a1_stream_queue_t *queue, uint64_t fid);

int a1_stream_queue_push(
    a1_stream_queue_t *queue,
    uint64_t fid,
    uint64_t timestamp,
    uint8_t type,
    const uint8_t *data,
    size_t data_length);

int a1_stream_queue_next_wire(
    a1_stream_queue_t *queue,
    uint8_t *output,
    size_t output_capacity,
    size_t *written);

int a1_stream_wire_decode(
    const uint8_t *body,
    size_t body_length,
    a1_stream_wire_view_t *view);

int a1_stream_type_flushes_batch(uint8_t type);

#endif
