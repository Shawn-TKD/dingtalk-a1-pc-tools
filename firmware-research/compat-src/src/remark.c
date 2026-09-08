#include "a1/remark.h"

#include <string.h>

void a1_remark_tracker_init(a1_remark_tracker_t *tracker)
{
    if (tracker != NULL) {
        memset(tracker, 0, sizeof(*tracker));
    }
}

bool a1_remark_track_button(
    a1_remark_tracker_t *tracker,
    uint64_t timestamp_seconds)
{
    if (tracker == NULL ||
        timestamp_seconds - tracker->last_timestamp_seconds <
            A1_REMARK_MIN_INTERVAL_SECONDS) {
        return false;
    }

    tracker->last_timestamp_seconds = timestamp_seconds;
    tracker->pending_timestamp_seconds = timestamp_seconds;
    return true;
}

uint64_t a1_remark_take_pending(a1_remark_tracker_t *tracker)
{
    uint64_t timestamp;

    if (tracker == NULL) {
        return 0;
    }
    timestamp = tracker->pending_timestamp_seconds;
    tracker->pending_timestamp_seconds = 0;
    return timestamp;
}

a1_remark_feedback_result_t a1_remark_handle_app_feedback(
    const a1_remark_t *remark,
    bool remark_enabled)
{
    a1_remark_feedback_result_t result = {500u, false};

    if (!remark_enabled || remark == NULL) {
        result.response_code = remark_enabled ? 500u : 408u;
        return result;
    }
    if (remark->type != A1_REMARK_TYPE_APP_FEEDBACK) {
        return result;
    }

    result.show_display_feedback = true;
    if (remark->fid != 0 && remark->timestamp_seconds != 0 &&
        remark->fid < remark->timestamp_seconds) {
        result.response_code = 200u;
    }
    return result;
}
