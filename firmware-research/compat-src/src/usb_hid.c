#include "a1/usb_hid.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static int valid_kind(uint8_t kind)
{
    return kind == A1_FRAME_REQUEST ||
           kind == A1_FRAME_NOTIFY ||
           kind == A1_FRAME_RESPONSE;
}

a1_usb_result_t a1_usb_frame_encode_header(
    uint8_t output[A1_FRAME_HEADER_SIZE],
    uint8_t kind,
    uint16_t command,
    uint8_t message_id,
    uint32_t body_length)
{
    if (output == NULL || !valid_kind(kind)) {
        return A1_USB_INVALID_ARGUMENT;
    }
    if (body_length > A1_USB_BODY_LIMIT) {
        return A1_USB_PACKET_TOO_LARGE;
    }

    output[0] = kind;
    write_le16(output + 1, command);
    output[3] = message_id;
    write_le32(output + 4, body_length);
    return A1_USB_OK;
}

a1_usb_result_t a1_usb_frame_decode(
    const uint8_t *bytes,
    size_t length,
    a1_frame_view_t *frame,
    size_t *consumed)
{
    uint32_t body_length;
    size_t total;

    if (bytes == NULL || frame == NULL || consumed == NULL) {
        return A1_USB_INVALID_ARGUMENT;
    }
    *consumed = 0;
    if (length < A1_FRAME_HEADER_SIZE) {
        return A1_USB_NEED_MORE;
    }
    if (!valid_kind(bytes[0])) {
        return A1_USB_INVALID_FRAME;
    }
    body_length = read_le32(bytes + 4);
    if (body_length > A1_USB_BODY_LIMIT) {
        return A1_USB_PACKET_TOO_LARGE;
    }
    total = A1_FRAME_HEADER_SIZE + (size_t)body_length;
    if (length < total) {
        return A1_USB_NEED_MORE;
    }

    frame->kind = bytes[0];
    frame->command = read_le16(bytes + 1);
    frame->message_id = bytes[3];
    frame->body_length = body_length;
    frame->body = bytes + A1_FRAME_HEADER_SIZE;
    *consumed = total;
    return A1_USB_OK;
}

size_t a1_usb_hid_report_count(size_t frame_length)
{
    if (frame_length == 0u || frame_length > A1_USB_PACKET_LIMIT) {
        return 0u;
    }
    return (frame_length + A1_USB_HID_PAYLOAD_SIZE - 1u) /
           A1_USB_HID_PAYLOAD_SIZE;
}

a1_usb_result_t a1_usb_hid_make_report(
    const uint8_t *frame,
    size_t frame_length,
    size_t report_index,
    uint8_t output[A1_USB_HID_REPORT_SIZE])
{
    size_t offset;
    size_t remaining;
    size_t chunk_length;
    size_t report_count;

    if (frame == NULL || output == NULL) {
        return A1_USB_INVALID_ARGUMENT;
    }
    report_count = a1_usb_hid_report_count(frame_length);
    if (report_count == 0u || report_index >= report_count) {
        return A1_USB_INVALID_ARGUMENT;
    }
    offset = report_index * A1_USB_HID_PAYLOAD_SIZE;
    remaining = frame_length - offset;
    chunk_length = remaining < A1_USB_HID_PAYLOAD_SIZE
        ? remaining
        : A1_USB_HID_PAYLOAD_SIZE;

    memset(output, 0, A1_USB_HID_REPORT_SIZE);
    output[0] = A1_USB_HID_REPORT_ID;
    memcpy(output + 1, frame + offset, chunk_length);
    return A1_USB_OK;
}

void a1_usb_hid_decoder_init(a1_usb_hid_decoder_t *decoder)
{
    if (decoder != NULL) {
        memset(decoder, 0, sizeof(*decoder));
    }
}

static a1_usb_result_t decoder_feed(
    a1_usb_hid_decoder_t *decoder,
    const uint8_t *report,
    size_t report_length,
    uint32_t now_ms,
    int use_timestamp,
    a1_frame_view_t *frame)
{
    size_t copy_length;
    size_t consumed;
    a1_usb_result_t result;

    if (decoder == NULL || report == NULL || frame == NULL ||
        report_length < 2u || report_length > A1_USB_HID_REPORT_SIZE) {
        return A1_USB_INVALID_ARGUMENT;
    }
    if (decoder->frame_ready) {
        return A1_USB_DECODER_BUSY;
    }
    if (report[0] != A1_USB_HID_REPORT_ID) {
        return A1_USB_WRONG_REPORT_ID;
    }

    if (use_timestamp && decoder->timestamp_valid &&
        (uint32_t)(now_ms - decoder->first_fragment_ms) >
            A1_USB_FRAGMENT_TIMEOUT_MS) {
        a1_usb_hid_decoder_init(decoder);
    }

    copy_length = report_length - 1u;
    if (decoder->expected != 0u &&
        copy_length > decoder->expected - decoder->buffered) {
        copy_length = decoder->expected - decoder->buffered;
    }
    if (copy_length > sizeof(decoder->buffer) - decoder->buffered) {
        return A1_USB_PACKET_TOO_LARGE;
    }
    memcpy(decoder->buffer + decoder->buffered, report + 1, copy_length);
    decoder->buffered += copy_length;

    if (use_timestamp && !decoder->timestamp_valid) {
        decoder->first_fragment_ms = now_ms;
        decoder->timestamp_valid = 1;
    }

    if (decoder->expected == 0u && decoder->buffered >= A1_FRAME_HEADER_SIZE) {
        if (!valid_kind(decoder->buffer[0])) {
            return A1_USB_INVALID_FRAME;
        }
        decoder->expected = A1_FRAME_HEADER_SIZE + read_le32(decoder->buffer + 4);
        if (decoder->expected > A1_USB_PACKET_LIMIT) {
            return A1_USB_PACKET_TOO_LARGE;
        }
    }
    if (decoder->expected == 0u || decoder->buffered < decoder->expected) {
        return A1_USB_NEED_MORE;
    }

    result = a1_usb_frame_decode(decoder->buffer, decoder->expected, frame, &consumed);
    if (result != A1_USB_OK || consumed != decoder->expected) {
        return result == A1_USB_OK ? A1_USB_INVALID_FRAME : result;
    }
    decoder->frame_ready = 1;
    return A1_USB_OK;
}

a1_usb_result_t a1_usb_hid_decoder_feed(
    a1_usb_hid_decoder_t *decoder,
    const uint8_t *report,
    size_t report_length,
    a1_frame_view_t *frame)
{
    return decoder_feed(decoder, report, report_length, 0u, 0, frame);
}

a1_usb_result_t a1_usb_hid_decoder_feed_at(
    a1_usb_hid_decoder_t *decoder,
    const uint8_t *report,
    size_t report_length,
    uint32_t now_ms,
    a1_frame_view_t *frame)
{
    return decoder_feed(decoder, report, report_length, now_ms, 1, frame);
}

void a1_usb_hid_decoder_consume(a1_usb_hid_decoder_t *decoder)
{
    a1_usb_hid_decoder_init(decoder);
}
