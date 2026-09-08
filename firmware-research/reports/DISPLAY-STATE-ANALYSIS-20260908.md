# DingTalk A1 V1.6.88 display-state table

`dt_display_test_ui_states` at AP `0x103e02c8` contains a concrete test table,
not merely diagnostic prose. The byte table at `0x104fff8c` supplies state IDs;
the pointer table at `0x104fff3c` supplies their names.

| ID | recovered name |
| ---: | --- |
| 7 | `BOOT_ANIMATION` |
| 14 | `STANDBY_WELCOME` |
| 16 | `PRESS_RELEASE_REMIND` |
| 8 | `ACTIVATION_REMIND` |
| 9 | `BT_ACTIVATING` |
| 10 | `ACTIVATION_SUCCESS` |
| 11 | `ACTIVATION_FAILED` |
| 24 | `RECORDING` |
| 25 | `RECORD_PAUSED` |
| 27 | `RECORD_STOP` |
| 29 | `SYNCING` |
| 30 | `SYNC_COMPLETE` |
| 31 | `SYNC_FAILED` |
| 22 | `WIFI_TRANSFER_FINISHED` |
| 38 | `LISTENING_ANIM` |
| 39 | `VOICE_MEMO_ANIM` |
| 40 | `VOICEPRINT_ANIM` |
| 41 | `DEVICE_UPGRADING` |
| 17 | `BATTERY_LOW_10` |
| 18 | `BATTERY_LOW_2SHUTTING_DOWN` |

The function iterates modulo 21, but the name-pointer table has only 20
entries. Its 21st lookup reads the first four bytes of the adjacent ID table as
the invalid pointer `0x08100e07`; the corresponding ID is 43. The compatible
implementation deliberately does not dereference or name that diagnostic-only
entry. This appears to be a stock test-helper array-length bug, not evidence of
a missing recoverable state name.
