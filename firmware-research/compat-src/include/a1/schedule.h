#ifndef A1_SCHEDULE_H
#define A1_SCHEDULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    A1_SCHEDULE_COMMAND = 0x011a,
    A1_SCHEDULE_VERSION = 1,
    A1_SCHEDULE_MAX_ENTRIES = 20,
    A1_SCHEDULE_FILE_HEADER_SIZE = 24,
    A1_SCHEDULE_FILE_ENTRY_SIZE = 24,
    A1_SCHEDULE_PRECISE_WINDOW_SECONDS = 120
};

#define A1_SCHEDULE_FILE_MAGIC UINT32_C(0x45484353) /* "SCHE" on disk */

typedef struct {
    uint64_t start_seconds;
    uint64_t end_seconds;
    uint16_t schedule_id;
    uint8_t reserved[6];
} a1_schedule_entry_t;

typedef struct {
    uint64_t current_seconds;
    uint32_t count;
    a1_schedule_entry_t entries[A1_SCHEDULE_MAX_ENTRIES];
} a1_schedule_t;

typedef enum {
    A1_SCHEDULE_OK = 0,
    A1_SCHEDULE_INVALID_ARGUMENT = -1,
    A1_SCHEDULE_INVALID_COUNT = -2,
    A1_SCHEDULE_TRUNCATED = -3,
    A1_SCHEDULE_BAD_MAGIC = -4,
    A1_SCHEDULE_BAD_VERSION = -5,
    A1_SCHEDULE_BAD_CHECKSUM = -6,
    A1_SCHEDULE_INVALID_ENTRY = -7
} a1_schedule_result_t;

typedef enum {
    A1_SCHEDULE_TIMER_NORMAL = 0,
    A1_SCHEDULE_TIMER_COARSE,
    A1_SCHEDULE_TIMER_PRECISE
} a1_schedule_timer_mode_t;

void a1_schedule_init(a1_schedule_t *schedule);
bool a1_schedule_entry_is_valid(const a1_schedule_entry_t *entry);

/* The BLE set path additionally rejects entries whose end is not later than
 * the supplied device clock. An empty list means clear and is valid here. */
int a1_schedule_validate_set(const a1_schedule_t *schedule);

/* Firmware persistence is native little-endian:
 * SCHE, version, current, count, additive checksum, then 24-byte entries. */
uint32_t a1_schedule_checksum(const uint8_t *bytes, size_t length);
size_t a1_schedule_storage_size(const a1_schedule_t *schedule);
int a1_schedule_encode(const a1_schedule_t *schedule,
                       uint8_t *output,
                       size_t capacity,
                       size_t *written);
int a1_schedule_decode(const uint8_t *input,
                       size_t length,
                       a1_schedule_t *schedule,
                       size_t *consumed);

/* Stock helper semantics: find the earliest valid start strictly after now,
 * and report whether any valid entry has an end strictly after now. */
bool a1_schedule_next_start(const a1_schedule_t *schedule,
                            uint64_t now_seconds,
                            uint64_t *start_seconds);
bool a1_schedule_has_unexpired(const a1_schedule_t *schedule,
                               uint64_t now_seconds);
a1_schedule_timer_mode_t a1_schedule_timer_mode(
    const a1_schedule_t *schedule,
    uint64_t now_seconds);

#endif
