#ifndef A1_REMARK_H
#define A1_REMARK_H

#include <stdbool.h>
#include <stdint.h>

enum {
    A1_REMARK_TYPE_DEVICE_MARK = 0,
    A1_REMARK_TYPE_APP_FEEDBACK = 2,
    A1_REMARK_MIN_INTERVAL_SECONDS = 3,
    A1_REMARK_FEEDBACK_ACTIVE_MS = 3000
};

typedef struct {
    uint64_t fid;
    uint64_t timestamp_seconds;
    uint32_t type;
} a1_remark_t;

typedef struct {
    uint64_t last_timestamp_seconds;
    uint64_t pending_timestamp_seconds;
} a1_remark_tracker_t;

typedef struct {
    uint16_t response_code;
    bool show_display_feedback;
} a1_remark_feedback_result_t;

void a1_remark_tracker_init(a1_remark_tracker_t *tracker);

/* Stock firmware keeps separate trackers for ordinary-record and streamed
 * frame marks. A mark less than three seconds after the previous one is
 * ignored; an accepted mark becomes pending for the next audio frame. */
bool a1_remark_track_button(
    a1_remark_tracker_t *tracker,
    uint64_t timestamp_seconds);
uint64_t a1_remark_take_pending(a1_remark_tracker_t *tracker);

/* App type 2 is the stock feedback channel. The display feedback is triggered
 * before payload validation; code 200 requires nonzero fid/ts and fid < ts. */
a1_remark_feedback_result_t a1_remark_handle_app_feedback(
    const a1_remark_t *remark,
    bool remark_enabled);

#endif
