#include "a1/display.h"

const char *a1_display_state_name(int state)
{
    switch (state) {
    case A1_DISPLAY_BOOT_ANIMATION: return "BOOT_ANIMATION";
    case A1_DISPLAY_ACTIVATION_REMIND: return "ACTIVATION_REMIND";
    case A1_DISPLAY_BT_ACTIVATING: return "BT_ACTIVATING";
    case A1_DISPLAY_ACTIVATION_SUCCESS: return "ACTIVATION_SUCCESS";
    case A1_DISPLAY_ACTIVATION_FAILED: return "ACTIVATION_FAILED";
    case A1_DISPLAY_STANDBY_WELCOME: return "STANDBY_WELCOME";
    case A1_DISPLAY_PRESS_RELEASE_REMIND: return "PRESS_RELEASE_REMIND";
    case A1_DISPLAY_BATTERY_LOW_10: return "BATTERY_LOW_10";
    case A1_DISPLAY_BATTERY_LOW_2_SHUTTING_DOWN:
        return "BATTERY_LOW_2SHUTTING_DOWN";
    case A1_DISPLAY_WIFI_TRANSFER_FINISHED: return "WIFI_TRANSFER_FINISHED";
    case A1_DISPLAY_RECORDING: return "RECORDING";
    case A1_DISPLAY_RECORD_PAUSED: return "RECORD_PAUSED";
    case A1_DISPLAY_RECORD_STOP: return "RECORD_STOP";
    case A1_DISPLAY_SYNCING: return "SYNCING";
    case A1_DISPLAY_SYNC_COMPLETE: return "SYNC_COMPLETE";
    case A1_DISPLAY_SYNC_FAILED: return "SYNC_FAILED";
    case A1_DISPLAY_CHARGING: return "CHARGING";
    case A1_DISPLAY_LISTENING_ANIM: return "LISTENING_ANIM";
    case A1_DISPLAY_VOICE_MEMO_ANIM: return "VOICE_MEMO_ANIM";
    case A1_DISPLAY_VOICEPRINT_ANIM: return "VOICEPRINT_ANIM";
    case A1_DISPLAY_DEVICE_UPGRADING: return "DEVICE_UPGRADING";
    default: return 0;
    }
}

bool a1_display_state_is_transient(int state)
{
    /* Bits set in the stock 0x0fd92307 mask are persistent states 16..43. */
    static const unsigned persistent_mask = 0x0fd92307u;
    unsigned offset;

    if (state < 0 || state > 44) {
        return false;
    }
    if (state < 12) {
        return state != A1_DISPLAY_BOOT_ANIMATION &&
               state != A1_DISPLAY_ACTIVATION_REMIND &&
               state != A1_DISPLAY_BT_ACTIVATING;
    }
    if (state < 16 || state > 43) {
        return true;
    }
    offset = (unsigned)(state - 16);
    return ((persistent_mask >> offset) & 1u) == 0u;
}

int a1_display_auto_return_target(
    int state,
    int normal_return_state,
    bool charging)
{
    if (!a1_display_state_is_transient(state)) {
        return state;
    }
    if (state == A1_DISPLAY_ACTIVATION_FAILED) {
        return A1_DISPLAY_ACTIVATION_REMIND;
    }
    return charging ? A1_DISPLAY_CHARGING : normal_return_state;
}
