# Reconstruction roadmap

The target is a functionally equivalent implementation of the A1 product layer,
not a byte-identical reproduction of the vendor firmware.

## Coverage vocabulary

- **Mapped**: entry point, callers, command/event values, and major side effects
  are supported by static evidence.
- **Modeled**: behavior exists in maintainable compatible source behind a HAL.
- **Observed**: the corresponding stock-device behavior has been reproduced on
  the owner's device without changing firmware.
- **Portable**: the compatible module builds without the BES BSP.
- **Device-ready**: the module has a BES/NuttX port and recovery-safe deployment
  has been demonstrated. Nothing is device-ready yet.

These words avoid treating Ghidra's successful C-text export as semantic source
recovery.

## Work order

1. Protocol foundation
   - BLE framing and command dispatch;
   - connection/authentication/session states;
   - response and notification ownership.
2. Audio behavior
   - ordinary recording state machine;
   - AI-key live stream versus offline voice memo;
   - marker timeline and voice-memo container lifecycle.
3. File transport
   - ordinary index and block synchronization;
   - raw transfer resume/CRC/retry behavior;
   - Wi-Fi AP and read-only HTTP handoff.
4. Device interaction
   - button event mapping and debounce/timing (portable policy modeled; target
     callback wiring pending);
   - display indications and vibration patterns;
   - battery, storage, charging, and power states.
5. Persistent behavior
   - audio settings and scheduled recordings;
   - identity/config boundaries without copying device secrets;
   - OTA state model, excluding signing or flashing until recovery is proven.
6. Target port
   - recover ROM/BSP import ABI and linker memory layout;
   - bind HAL to NuttX/BES services;
   - only then evaluate a recovery-safe experimental image.

## Current milestone

The portable core now models framing, the command registry, challenge/token
authentication, logical and physical BLE connection state, recording actions
and settings, the complete two-button policy, memo and ordinary recording
containers, file/raw/live-stream transports, Wi-Fi HTTP/TCP transport, USB HID,
display, marker, schedule, gray-switch and recording-lifecycle behavior. A
bounded internal JSON reader and typed decoders now cover the principal stock
request schemas, including `0x0133`, `0x0100`, `0x0101`, `0x0111`, `0x0113`,
`0x011a`, `0x0120`, `0x0136`, `0x0137`, and `0x014a`.

The response serializer, portable application service, streaming BLE endpoint
and persistence HAL are now implemented. Its host test replays fragmented and
coalesced frames, challenge/token connection, recording and live-stream
start/stop, persisted audio/gray/schedule settings with write-failure rollback,
file list/sync/cancel/delete, raw transfer, marker acknowledgement, battery and
firmware queries, Wi-Fi open/close, disconnect and the stock unknown-command
response. The service covers 22 of the 27 recovered request commands; the
complete list and the five intentionally isolated provisioning/reset commands
are recorded in `COMMAND-REGISTRY.md`. Inbound
OTA/user-image headers and blocks terminate in a staging-and-verification HAL;
the portable service has no operation capable of applying firmware.

The identity/status response surface is implemented. The current pass is the
BES/NuttX target ABI inventory: exact NOR geometry and AP/APC1/HiFi image-header
validation are modeled. The AP-side RPTUN object layout, resource selector,
local vring arenas, RX/TX IRQ directions and portable RMT IPC queue/flow-control
behavior are now modeled. The official openvela BEST1700 library release has
also been compared at object-code level: 139 relocation-masked symbols uniquely
match the A1 AP image, including CMU, IOMUX, USB, key, storage and mailbox HAL
functions. This gives the target port a plausible same-chip BSP base, but the
A1-specific `dtiot_2800hp` board layer, DSP/BTH peer resource addresses, boot
selection and signature enforcement remain to be recovered.
Host socket adapters follow this boundary work. Provisioning, OTA application
and factory-reset paths stay separate until target recovery and signature
behavior are proven.
