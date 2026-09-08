#include "a1/schedule.h"

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

static uint64_t read_le64(const uint8_t *bytes)
{
    return (uint64_t)read_le32(bytes) |
           ((uint64_t)read_le32(bytes + 4) << 32);
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

static void write_le64(uint8_t *bytes, uint64_t value)
{
    write_le32(bytes, (uint32_t)value);
    write_le32(bytes + 4, (uint32_t)(value >> 32));
}

void a1_schedule_init(a1_schedule_t *schedule)
{
    if (schedule != NULL) {
        memset(schedule, 0, sizeof(*schedule));
    }
}

bool a1_schedule_entry_is_valid(const a1_schedule_entry_t *entry)
{
    return entry != NULL && entry->start_seconds != 0 &&
           entry->start_seconds < entry->end_seconds;
}

int a1_schedule_validate_set(const a1_schedule_t *schedule)
{
    uint32_t index;

    if (schedule == NULL) {
        return A1_SCHEDULE_INVALID_ARGUMENT;
    }
    if (schedule->count > A1_SCHEDULE_MAX_ENTRIES) {
        return A1_SCHEDULE_INVALID_COUNT;
    }
    for (index = 0; index < schedule->count; ++index) {
        const a1_schedule_entry_t *entry = &schedule->entries[index];
        if (!a1_schedule_entry_is_valid(entry) ||
            schedule->current_seconds >= entry->end_seconds) {
            return A1_SCHEDULE_INVALID_ENTRY;
        }
    }
    return A1_SCHEDULE_OK;
}

uint32_t a1_schedule_checksum(const uint8_t *bytes, size_t length)
{
    uint32_t sum = 0;
    size_t index;

    if (bytes == NULL) {
        return 0;
    }
    for (index = 0; index < length; ++index) {
        sum += bytes[index];
    }
    return sum;
}

size_t a1_schedule_storage_size(const a1_schedule_t *schedule)
{
    if (schedule == NULL || schedule->count == 0 ||
        schedule->count > A1_SCHEDULE_MAX_ENTRIES) {
        return 0;
    }
    return A1_SCHEDULE_FILE_HEADER_SIZE +
           (size_t)schedule->count * A1_SCHEDULE_FILE_ENTRY_SIZE;
}

int a1_schedule_encode(const a1_schedule_t *schedule,
                       uint8_t *output,
                       size_t capacity,
                       size_t *written)
{
    size_t body_size;
    size_t total_size;
    uint32_t index;

    if (schedule == NULL || output == NULL || written == NULL) {
        return A1_SCHEDULE_INVALID_ARGUMENT;
    }
    total_size = a1_schedule_storage_size(schedule);
    if (total_size == 0) {
        return A1_SCHEDULE_INVALID_COUNT;
    }
    if (capacity < total_size) {
        return A1_SCHEDULE_TRUNCATED;
    }

    memset(output, 0, total_size);
    write_le32(output, A1_SCHEDULE_FILE_MAGIC);
    write_le32(output + 4, A1_SCHEDULE_VERSION);
    write_le64(output + 8, schedule->current_seconds);
    write_le32(output + 16, schedule->count);

    for (index = 0; index < schedule->count; ++index) {
        uint8_t *encoded = output + A1_SCHEDULE_FILE_HEADER_SIZE +
                           (size_t)index * A1_SCHEDULE_FILE_ENTRY_SIZE;
        const a1_schedule_entry_t *entry = &schedule->entries[index];
        write_le64(encoded, entry->start_seconds);
        write_le64(encoded + 8, entry->end_seconds);
        write_le16(encoded + 16, entry->schedule_id);
        memcpy(encoded + 18, entry->reserved, sizeof(entry->reserved));
    }

    body_size = total_size - A1_SCHEDULE_FILE_HEADER_SIZE;
    write_le32(output + 20,
               a1_schedule_checksum(output + A1_SCHEDULE_FILE_HEADER_SIZE,
                                    body_size));
    *written = total_size;
    return A1_SCHEDULE_OK;
}

int a1_schedule_decode(const uint8_t *input,
                       size_t length,
                       a1_schedule_t *schedule,
                       size_t *consumed)
{
    uint32_t count;
    uint32_t expected_checksum;
    size_t body_size;
    size_t total_size;
    uint32_t index;

    if (input == NULL || schedule == NULL || consumed == NULL) {
        return A1_SCHEDULE_INVALID_ARGUMENT;
    }
    if (length < A1_SCHEDULE_FILE_HEADER_SIZE) {
        return A1_SCHEDULE_TRUNCATED;
    }
    if (read_le32(input) != A1_SCHEDULE_FILE_MAGIC) {
        return A1_SCHEDULE_BAD_MAGIC;
    }
    if (read_le32(input + 4) != A1_SCHEDULE_VERSION) {
        return A1_SCHEDULE_BAD_VERSION;
    }
    count = read_le32(input + 16);
    if (count == 0 || count > A1_SCHEDULE_MAX_ENTRIES) {
        return A1_SCHEDULE_INVALID_COUNT;
    }
    body_size = (size_t)count * A1_SCHEDULE_FILE_ENTRY_SIZE;
    total_size = A1_SCHEDULE_FILE_HEADER_SIZE + body_size;
    if (length < total_size) {
        return A1_SCHEDULE_TRUNCATED;
    }
    expected_checksum = read_le32(input + 20);
    if (a1_schedule_checksum(input + A1_SCHEDULE_FILE_HEADER_SIZE,
                             body_size) != expected_checksum) {
        return A1_SCHEDULE_BAD_CHECKSUM;
    }

    a1_schedule_init(schedule);
    schedule->current_seconds = read_le64(input + 8);
    schedule->count = count;
    for (index = 0; index < count; ++index) {
        const uint8_t *encoded = input + A1_SCHEDULE_FILE_HEADER_SIZE +
                                 (size_t)index * A1_SCHEDULE_FILE_ENTRY_SIZE;
        a1_schedule_entry_t *entry = &schedule->entries[index];
        entry->start_seconds = read_le64(encoded);
        entry->end_seconds = read_le64(encoded + 8);
        entry->schedule_id = read_le16(encoded + 16);
        memcpy(entry->reserved, encoded + 18, sizeof(entry->reserved));
    }
    *consumed = total_size;
    return A1_SCHEDULE_OK;
}

bool a1_schedule_next_start(const a1_schedule_t *schedule,
                            uint64_t now_seconds,
                            uint64_t *start_seconds)
{
    bool found = false;
    uint64_t next = 0;
    uint32_t index;

    if (schedule == NULL) {
        return false;
    }
    for (index = 0; index < schedule->count &&
                    index < A1_SCHEDULE_MAX_ENTRIES; ++index) {
        const a1_schedule_entry_t *entry = &schedule->entries[index];
        if (a1_schedule_entry_is_valid(entry) &&
            now_seconds < entry->start_seconds &&
            (!found || entry->start_seconds < next)) {
            found = true;
            next = entry->start_seconds;
        }
    }
    if (found && start_seconds != NULL) {
        *start_seconds = next;
    }
    return found;
}

bool a1_schedule_has_unexpired(const a1_schedule_t *schedule,
                               uint64_t now_seconds)
{
    uint32_t index;

    if (schedule == NULL) {
        return false;
    }
    for (index = 0; index < schedule->count &&
                    index < A1_SCHEDULE_MAX_ENTRIES; ++index) {
        const a1_schedule_entry_t *entry = &schedule->entries[index];
        if (a1_schedule_entry_is_valid(entry) &&
            now_seconds < entry->end_seconds) {
            return true;
        }
    }
    return false;
}

a1_schedule_timer_mode_t a1_schedule_timer_mode(
    const a1_schedule_t *schedule,
    uint64_t now_seconds)
{
    uint64_t next;

    if (!a1_schedule_has_unexpired(schedule, now_seconds)) {
        return A1_SCHEDULE_TIMER_NORMAL;
    }
    if (a1_schedule_next_start(schedule, now_seconds, &next) &&
        next - now_seconds <= A1_SCHEDULE_PRECISE_WINDOW_SECONDS) {
        return A1_SCHEDULE_TIMER_PRECISE;
    }
    return A1_SCHEDULE_TIMER_COARSE;
}
