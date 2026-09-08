# BES/NuttX target-port contract

This directory defines the boundary between the portable compatible product
core and a future A1 hardware build. It is an evidence-backed porting plan, not
a claim that the vendor BSP or a recovery-safe linker layout has been recovered.

The portable core must never call functions at fixed V1.6.88 addresses. Those
addresses are evidence labels only: a rebuilt image changes layout, and a
partially overlaid image would make ownership, interrupt and RTOS state unsafe.
The target port instead implements `a1_platform_t` and `a1_persistence_ops_t`
against a documented BES/NuttX SDK.

## Required platform groups

| Core callback group | V1.6.88 behavior used as evidence | Target dependency | Port status |
| --- | --- | --- | --- |
| BLE response transport | AP `0x103ca120`, characteristic path AP `0x103cec74`, frame packer AP `0x103cf0ac` | GATT service registration, notify/write callbacks, connection MTU | contract defined; BSP binding missing |
| Random and AES | AP `0x103e9d00`, `0x103e9d7c`; challenge path AP `0x103ca9e0`/`0x103ca86c` | hardware security engine or audited crypto library | contract defined; BSP binding missing |
| Ordinary recording | start AP `0x103d77b0`, pause `0x103d6ee4`, resume `0x103d6f78`, stop `0x103d700c` | microphone/codec/DMA, Opus encoder, file writer | policy modeled; audio driver binding missing |
| Live stream | start AP `0x103d7970`, stop `0x103d7360`, packet path AP `0x103c6684` | encoder callback and BLE notification queue | wire/state modeled; encoder binding missing |
| Voice memo | start AP `0x103d49b4`, stop AP `0x103d4d40`, flush AP `0x103d435c` | same capture path plus aggregate-file writer | container modeled; capture binding missing |
| Recording index/file sync | list AP `0x103e17b0`, setup AP `0x103e2104`, blocks AP `0x103e1374`, cancel AP `0x103e20c0`, delete AP `0x103e2280` | EMMC filesystem and asynchronous worker | codecs/state modeled; worker binding missing |
| Raw file read | AP `0x103e4bd0`, block AP `0x103e477c`, retry AP `0x103e4964` | bounded read-only path policy and filesystem worker | codecs/state modeled; worker binding missing |
| Display and vibration | display loop AP `0x103e0598`, motor AP `0x103c7d84` | display bus/framebuffer, motor GPIO/PWM | policy modeled; pin/display binding missing |
| Buttons | callback AP `0x103d7554`, state machine AP `0x103d896c` | GPIO interrupt/debounce clock and work queue | complete portable policy; GPIO binding missing |
| Wi-Fi transfer | AP create/close `0x103c8290`/`0x103c83fc`, TCP server AP `0x103e5574` | Wi-Fi AP driver, DHCP, sockets, HTTP/file workers | protocol/lifecycle modeled; network binding missing |
| Battery/storage/power | battery AP `0x103c500c`, statvfs AP `0x103c85cc`, system control AP `0x103cd510` | PMU, `statvfs`, orderly power/reboot hooks | values/plans modeled; PMU binding missing |
| Inbound staging | BLE AP `0x103cbb78`/`0x103cbe58`, OTA AP `0x103c5fcc`/`0x103c60f4` | temporary file, flush, MD5 calculation | receive/verify contract modeled; applying OTA intentionally absent |

## Persistence contract

`a1_persistence_ops_t` maps scalar settings to the recovered `persist.dt.*`
keys and schedules to `/emmc/schedule/schedule.dat`. A target backend should
provide atomic multi-key commit or a two-slot journal. The portable service can
restore its in-memory state after an I/O error, but it cannot undo a subset of
physical key writes already committed by a non-transactional backend.

Identity storage is intentionally outside this interface. The 64-KiB DTIOT
configuration area, factory SN/MAC fields, random binding secret, and derived
`deviceSecret` have different lifetimes and must not be erased or regenerated
as a side effect of normal settings persistence.

## Target initialization order

1. Bring up clocks, heap, NuttX work queues and EMMC read-only.
2. Read immutable factory identity and existing DTIOT binding state without
   modifying either region.
3. Initialize PMU, buttons, display/motor, audio and BLE drivers.
4. Construct `a1_platform_t`; validate every enabled feature group.
5. Initialize `a1_protocol_service_t` with the existing derived protocol key.
6. Attach persistence and load settings/schedule into temporary objects.
7. Register the BLE write callback through `a1_ble_endpoint_feed()` and encode
   responses through the platform notification callback.
8. Start advertising only after the service, storage and callback ownership are
   valid. Wi-Fi and file workers remain lazy.

## Link and recovery blockers

The following artifacts are still required before a hardware image is honest:

- a matching BES2800HP/BEST1700 compiler ABI and vendor driver libraries. The
  openvela `vendor_bes` BEST1700 library release now supplies a strong local
  candidate: its linker script uses the exact A1 AP origin `0x10190000`, and
  relocation-masked comparison uniquely maps 139 library symbols into the
  V1.6.88 AP image. It is for the `aos_evb`, not the A1 `dtiot_2800hp`, and its
  restrictive BES license means it remains a separately obtained BSP rather
  than source copied into this tree. See
  `../../../reports/BEST1700-BSP-BRIDGE-ANALYSIS-20260908.md`;
- runtime IRQ ownership, complete memory-region ownership and ROM/BSP startup
  ABI; the AP/APC1 Flash bases, entry paths, vector initialization, stacks,
  data-copy and BSS ranges are now recovered in
  `FIRMWARE-LAYOUT-ANALYSIS-20260908.md`;
- the AP-side RPTUN/OpenAMP object layout, core/channel/resource IDs and local
  vring arenas are recovered in `RPTUN-ABI-ANALYSIS-20260908.md`; the portable
  active/pending message queues and receive flow control now live in
  `a1/rmt_ipc.h`, and RX/TX IRQ lines plus all nine stock board-callback
  addresses are identified. Formal CMU bit names, DSP/BTH peer resource
  addresses and compatible companion images remain missing;
- boot-slot selection behavior; the 12-entry NOR partition geometry is now
  recovered and modeled in `a1/flash_layout.h`;
- image/OTA signature verification and a proven ROM recovery entry;
- a complete readback of boot, OTA, factory and DTIOT areas from the owner's
  device, stored separately from source control.

Until those are recovered, `compat-src` is a compilable host/application core,
not a safe replacement firmware. The first target milestone is a no-flash build
linked against a documented BSP and exercised under an emulator or vendor dev
board; the A1 itself comes only after a verified full backup and recovery test.
