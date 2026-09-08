# DingTalk A1 compatible application core

This directory is a clean, maintainable reimplementation of the product-facing
logic recovered from DingTalk A1 V1.6.88. It is not the original source code and
does not contain the vendor BSP, firmware signing keys, or a flashing tool.

The first milestone deliberately separates three layers:

1. `core`: portable protocol and product state machines;
2. `platform`: a narrow HAL for BLE responses, recording, voice memo, live audio,
   vibration, reboot, storage cleanup, and logging;
3. `ports`: future host/NuttX/BES integrations.

This lets the recovered behavior be compiled and tested on a host before any
device firmware is changed. A BES/NuttX port will only be added after the boot,
signature, and recovery path is understood.

## Current implemented scope

- BLE 8-byte header encode/decode (`kind | command:u16be | id | length:u32be`);
- owner-compatible challenge/token calculation behind random/AES HAL callbacks;
- exact hardware identity derivation for the protocol `deviceSecret`, with the
  unrelated random DTIOT binding secret kept as a separate concept;
- typed logical connect/disconnect behavior with recovered response codes;
- physical BLE link lifecycle, including the single-peer cache, advertising,
  Wi-Fi/file-sync cleanup and ADB-mode exception, plus exact phone-family
  classification and the iPhone/iPad initial connection parameters;
- complete V1.6.88 request-command registry and risk classification;
- table-driven routing, including the observed unsupported-command response;
- an inspectable system-control plan for recovered keys `1..5` and `10001`;
  destructive operations stay behind an explicit platform boundary;
- recovered two-button event policy (event values `0..9`) with the stock 390 ms,
  790 ms, and 5 s timing thresholds, marker/pause/resume decisions, and a
  cancellable long-long-press shutdown confirmation;
- confirmed AI-key long-press behavior:
  - logical BLE session connected -> live chat stream;
  - no logical BLE session -> local voice memo;
  - release stops whichever path was started;
- typed `0x0100` recording actions and validated audio settings, kept separate
  from unknown boot defaults, which are not invented;
- a dependency-free, bounded JSON reader plus typed request decoders for
  connect/auth metadata, audio/voiceprint actions, all eight recovered audio
  settings, file sync/delete, remarks, AP mode, raw-file reads, system control,
  scheduled recording, and gray-switch operations;
- a portable command service that replays complete challenge/connect/disconnect
  sessions and dispatches audio, voiceprint, status, Wi-Fi, schedule,
  gray-switch, marker, ordinary-file and raw-file operations through typed HAL
  callbacks while preserving asynchronous message IDs;
- a streaming BLE endpoint that accepts fragmented or coalesced characteristic
  writes, validates direction and payload limits, and emits complete framed
  responses;
- byte-accurate voice-memo aggregate and entry header codecs;
- ordinary recording index and file-block decoders, plus duplicate/order/size
  tracking for safe streamed downloads;
- the persistent audio database's eight-byte header/record codecs, 40-bit FIDs,
  tombstones, 1,000-record segment geometry and stock compaction threshold;
- recording lifecycle policy for 1,024-byte cache flushes, the 80-byte container
  header, rotation/final small-file deletion, early database insertion and the
  recovered free-space cleanup/reject thresholds;
- raw-transfer block codec with the recovered 8000-byte limit and CRC-32/BZIP2;
- USB little-endian control framing and 128-byte HID report segmentation/
  reassembly for recovered commands 400, 401, 402, and 403;
- live `0x0117` Opus-unit decoding and conservative `0x000c` telemetry parsing,
  including the exact subset that can be classified as a marker;
- device-side live-stream queuing and `0x0117` wire packing: 200-byte source
  fragments, 50-entry backpressure, 10,000-byte batches, boundary frame types,
  the 64-bit FID/timestamp/sequence header and packed 21-bit drop counter;
- BABA/DTYJ recording parsing, Opus TOC duration calculation, and standards-
  compliant Ogg page construction with the required Ogg checksum;
- Wi-Fi AP lifecycle with stock-compatible credentials, HTTP-audio port 80 and
  binary file-transfer port 5922, plus file-sync cancellation/performance HALs;
- the port-5922 streaming decoder with the stock 40-KiB connection buffer,
  request/response-kind gate, `0x0114`/`0x0115` dispatch allowlist, and the
  inbound OTA block's four big-endian fields plus CRC-32/BZIP2 validation;
- platform-mediated vibration, available internally but not exposed as a stock
  BLE command.
- the 20 names from the stock diagnostic display table, the independently
  evidenced charging state 37, and the recovered 3-second auto-return policy;
  the malformed extra diagnostic entry remains unnamed.
- typed device identity and status models shared by BLE/USB adapters, including
  the five stock audio-status strings and exact statvfs-to-MiB conversion.
- an inspectable system-control plan for stock keys `1..5` and `10001`, with
  destructive actions and their before/after-response ordering kept explicit.
- the marker/remark round trip: three-second device debounce, pending-frame
  consumption, outbound type-0 event, and app type-2 display acknowledgement.
- scheduled recording persistence and selection: exact `SCHE` version-one
  header, 24-byte entries, additive checksum, 20-entry validation, earliest
  future start, and the 120-second precise-timer boundary.
- optional persistent-state HALs for recovered audio and gray-switch keys plus
  schedule storage, with startup loading and service-state rollback on failed
  writes;
- firmware-query parsing and response behavior, including component-wise
  version comparison and an OTA-resume-offset HAL; receiving or applying an OTA
  image remains deliberately outside the normal command service;
- staging-only inbound OTA/user-image reception for commands `0x0114` and
  `0x0115`: exact header fields and response types, query/resume gate, binary
  block CRC and length checks, bounded receive accounting, and final verify-hash
  comparison; the HAL deliberately cannot install or flash the staged image;
- the exact 12-entry V1.6.88 16-MiB NOR partition geometry as a read-only,
  bounds-checked model; no erase or program operation is exposed;
- AP/APC1 download-versus-installed marker parsing and the recovered HiFi4
  boot/memory-map header, so host packaging tools can reject malformed images;
- the recovered AP-side companion-core manifest for DSPC0, BTHC0, and M55C1,
  including BES `rmt_ipc` core/channel/resource IDs, RPTUN ops/state addresses,
  8-entry/512-byte vring arena sizing, and the `0xd0` resource-table selector;
  this is read-only port evidence and performs no target-memory access;
- a clean portable RMT IPC channel implementing the recovered active/pending
  send lists, per-core send-slot counts, borrowed-buffer completion, RX partial-
  consume flow control, manual receive completion and IRQ lifecycle; CMU/NVIC
  register access remains a target-port responsibility;
- gray-switch feature state and its privileged control-key behavior as an
  inspectable plan; the compatible core never executes the stock arbitrary
  shell-command branch.

See `docs/TRACEABILITY.md` for the address/evidence map. Unverified behavior is
kept out of the executable core rather than silently guessed.

## Build shape

The sources are ISO C11 and intentionally have no external JSON or RTOS
dependency. The bounded protocol JSON reader/writer and command service are
part of the core; a target adapter only needs to bind typed requests and JSON
responses to NuttX/BES services. The compatible service never executes the
stock gray-control arbitrary-shell branch.

```sh
cmake -S . -B build
cmake --build build
```

The portable core and focused protocol tests compile cleanly as ISO C11 with
Zig Clang on Windows. The tests exercise known byte fixtures and the recovered
state transitions; this is host verification, not yet a BES/NuttX device port.

The hardware boundary and the artifacts still needed for a real image are
listed in `ports/nuttx_bes/PORTING.md`. The same-chip BSP/object comparison is
recorded in `../reports/BEST1700-BSP-BRIDGE-ANALYSIS-20260908.md`; vendor libraries are
not redistributed with this compatible source tree. Firmware addresses are
traceability evidence and are never called directly by compatible code.
