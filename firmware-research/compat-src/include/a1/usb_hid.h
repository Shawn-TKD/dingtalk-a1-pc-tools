#ifndef A1_USB_HID_H
#define A1_USB_HID_H

#include <stddef.h>
#include <stdint.h>

#include "a1/frame.h"

#define A1_USB_COMMAND_INFO 400u
#define A1_USB_COMMAND_AUTH 401u
#define A1_USB_COMMAND_WORK_MODE 402u
#define A1_USB_COMMAND_CHALLENGE 403u

#define A1_USB_PACKET_LIMIT 1024u
#define A1_USB_BODY_LIMIT (A1_USB_PACKET_LIMIT - A1_FRAME_HEADER_SIZE)
#define A1_USB_HID_REPORT_ID 1u
#define A1_USB_HID_REPORT_SIZE 128u
#define A1_USB_HID_PAYLOAD_SIZE 127u
#define A1_USB_FRAGMENT_TIMEOUT_MS 5000u

typedef enum {
    A1_USB_OK = 0,
    A1_USB_NEED_MORE = 1,
    A1_USB_INVALID_ARGUMENT = -1,
    A1_USB_WRONG_REPORT_ID = -2,
    A1_USB_INVALID_FRAME = -3,
    A1_USB_PACKET_TOO_LARGE = -4,
    A1_USB_DECODER_BUSY = -5
} a1_usb_result_t;

typedef struct {
    uint8_t buffer[A1_USB_PACKET_LIMIT];
    size_t buffered;
    size_t expected;
    uint32_t first_fragment_ms;
    int timestamp_valid;
    int frame_ready;
} a1_usb_hid_decoder_t;

a1_usb_result_t a1_usb_frame_encode_header(
    uint8_t output[A1_FRAME_HEADER_SIZE],
    uint8_t kind,
    uint16_t command,
    uint8_t message_id,
    uint32_t body_length);

a1_usb_result_t a1_usb_frame_decode(
    const uint8_t *bytes,
    size_t length,
    a1_frame_view_t *frame,
    size_t *consumed);

size_t a1_usb_hid_report_count(size_t frame_length);

a1_usb_result_t a1_usb_hid_make_report(
    const uint8_t *frame,
    size_t frame_length,
    size_t report_index,
    uint8_t output[A1_USB_HID_REPORT_SIZE]);

void a1_usb_hid_decoder_init(a1_usb_hid_decoder_t *decoder);

a1_usb_result_t a1_usb_hid_decoder_feed(
    a1_usb_hid_decoder_t *decoder,
    const uint8_t *report,
    size_t report_length,
    a1_frame_view_t *frame);

/* Stock V1.6.88 expires an unfinished frame more than five seconds after its
 * first fragment. The caller supplies a wrapping monotonic millisecond tick. */
a1_usb_result_t a1_usb_hid_decoder_feed_at(
    a1_usb_hid_decoder_t *decoder,
    const uint8_t *report,
    size_t report_length,
    uint32_t now_ms,
    a1_frame_view_t *frame);

void a1_usb_hid_decoder_consume(a1_usb_hid_decoder_t *decoder);

#endif
