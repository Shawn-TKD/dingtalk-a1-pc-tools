#ifndef A1_WIFI_TCP_H
#define A1_WIFI_TCP_H

#include <stddef.h>
#include <stdint.h>

#include "a1/frame.h"

#define A1_WIFI_TCP_PORT 5922u
#define A1_WIFI_TCP_BUFFER_SIZE 40960u
#define A1_WIFI_TCP_MAX_BODY_SIZE (A1_WIFI_TCP_BUFFER_SIZE - A1_FRAME_HEADER_SIZE - 1u)
#define A1_WIFI_TCP_FILE_HEADER_COMMAND 0x0114u
#define A1_WIFI_TCP_FILE_BLOCK_COMMAND 0x0115u
#define A1_WIFI_TCP_OTA_BLOCK_HEADER_SIZE 16u

typedef enum {
    A1_WIFI_TCP_OK = 0,
    A1_WIFI_TCP_NEED_MORE = 1,
    A1_WIFI_TCP_INVALID_ARGUMENT = -1,
    A1_WIFI_TCP_INVALID_KIND = -2,
    A1_WIFI_TCP_PACKET_TOO_LARGE = -3,
    A1_WIFI_TCP_BUFFER_FULL = -4,
    A1_WIFI_TCP_FAULTED = -5
} a1_wifi_tcp_result_t;

/* A returned frame view remains valid until the next call to
 * a1_wifi_tcp_decoder_next(), reset(), or feed() after the decoder has no
 * remaining capacity. */
typedef struct {
    uint8_t buffer[A1_WIFI_TCP_BUFFER_SIZE];
    size_t length;
    size_t returned_length;
    int faulted;
} a1_wifi_tcp_decoder_t;

typedef struct {
    uint32_t transfer_type;
    uint32_t crc;
    uint32_t sequence;
    uint32_t data_length;
    const uint8_t *data;
} a1_wifi_tcp_ota_block_view_t;

void a1_wifi_tcp_decoder_init(a1_wifi_tcp_decoder_t *decoder);
void a1_wifi_tcp_decoder_reset(a1_wifi_tcp_decoder_t *decoder);

a1_wifi_tcp_result_t a1_wifi_tcp_decoder_feed(
    a1_wifi_tcp_decoder_t *decoder,
    const uint8_t *bytes,
    size_t length);

a1_wifi_tcp_result_t a1_wifi_tcp_decoder_next(
    a1_wifi_tcp_decoder_t *decoder,
    a1_frame_view_t *frame);

int a1_wifi_tcp_command_supported(uint16_t command);

int a1_wifi_tcp_ota_block_decode(
    const uint8_t *body,
    size_t body_length,
    a1_wifi_tcp_ota_block_view_t *block);

int a1_wifi_tcp_ota_block_encode(
    uint8_t *output,
    size_t output_capacity,
    uint32_t sequence,
    const uint8_t *data,
    size_t data_length,
    size_t *written);

#endif
