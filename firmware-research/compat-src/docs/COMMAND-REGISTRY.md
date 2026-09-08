# A1 V1.6.88 BLE request command registry

The stock AP request switch at `0x103ce7b8` recognizes the 27 commands below.
The compatible service implements 22 of them. "Implemented" means that a
typed decoder and a bounded, testable product behavior exist; it does not mean
that a host build can physically operate A1 peripherals without a target HAL.

## Implemented commands (22/27)

| Command | Compatible name | Meaning | Current boundary |
| ---: | --- | --- | --- |
| `0x0003` | `device_info` | Read model, serial, firmware and capability/status data | response serializer implemented |
| `0x0008` | `auth_random` | Obtain the random challenge used by the logical-session handshake | challenge generation implemented |
| `0x0100` | `audio` | Start, stop, pause or resume recording; read/write audio settings | state/persistence/HAL plan implemented |
| `0x0101` | `voiceprint` | Start or stop the BLE live-audio stream | state and target HAL boundary implemented |
| `0x0102` | `remark` | Complete the App/device marker feedback loop | timestamp/type validation implemented |
| `0x0110` | `file_list` | Read the ordinary-recording index | index and list response implemented |
| `0x0111` | `file_sync` | Start/resume an ordinary recording download by FID | transfer state and platform callback implemented |
| `0x0112` | `file_sync_cancel` | Cancel an ordinary recording download | cancellation path implemented |
| `0x0113` | `file_delete` | Delete one ordinary recording by FID | destructive HAL boundary implemented |
| `0x0114` | `file_header` | Receive and validate an OTA/user-image transfer header | staging-only; cannot install firmware |
| `0x0115` | `file_block` | Receive, sequence-check and CRC-check an OTA/user-image block | staging-only; cannot install firmware |
| `0x011A` | `schedule_recording` | Set or query scheduled recording entries | parser, persistence and timer policy implemented |
| `0x0120` | `open_ap` | Start the device Wi-Fi AP and select its transfer mode | lifecycle/HAL plan implemented |
| `0x0121` | `close_ap` | Stop the device Wi-Fi AP and transfer server | lifecycle/HAL plan implemented |
| `0x0132` | `audio_status` | Read recording state, duration, FID, battery, storage and version | response serializer implemented |
| `0x0133` | `connect_device` | Verify DID/token and establish the logical BLE session | challenge-token session implemented |
| `0x0134` | `disconnect_device` | End the logical BLE session for the active DID | session cleanup implemented |
| `0x0135` | `firmware_version` | Query current/target versions and OTA resume offset | query model implemented; no updater |
| `0x0136` | `system_control` | Decode keys for cleanup, log-recording, OTA event and power actions | typed action plan behind target HAL |
| `0x0137` | `gray_switch` | Read/write recovered `persist.dt.*` feature switches | typed state and persistence implemented |
| `0x014A` | `raw_transfer` | Read a bounded known path with offset/resume and CRC blocks | read-only state machine implemented |
| `0x014C` | `raw_transfer_cancel` | Cancel a raw-path read | cancellation path implemented |

`0x014B` is the device-to-client raw data-block event paired with `0x014A`;
it is not one of the 27 inbound request commands.

## Registered but deliberately not implemented (5/27)

| Command | Stock handler | Reason it remains isolated |
| ---: | --- | --- |
| `0x0004` | `dtiot_ble_bind_reset_device` | Clears binding/identity state and reboots; unsafe before factory and recovery boundaries are complete |
| `0x0006` | `dtiot_ble_bind_get_active_info` | Activation schema is partly recovered but is coupled to provisioning state not needed for an already owned offline device |
| `0x0007` | `dtiot_ble_bind_active_device` | Writes activation, identity and transport fields; implementing it prematurely could replace working credentials |
| `0x0009` | `dtiot_ble_bind_auth` | Legacy/provisioning authentication path is separate from the verified `0x0008` + `0x0133` offline session flow |
| `0x0130` | `dtiot_ble_bind_on_get_trans_info` | Returns sensitive transport/provisioning identity; excluded from the public compatible service |

These five omissions do not prevent the verified owner-device workflow:
challenge, logical connection, status, recording control, list, download,
cancel, delete, marker, raw read and Wi-Fi lifecycle use the implemented set.
They matter when recreating factory activation, re-provisioning or reset.

## Response and transport-only command values

Other values appear on the wire without being independent inbound requests.
Examples include live audio `0x0117`, raw data block `0x014B`, raw ACK/retry
`0x0193`, and batched telemetry `0x000C`. Their decoders/state machines are
tracked separately from the 27-entry request registry.
