# V1.6.88 traceability map

This file separates confirmed evidence from clean-room implementation choices.

| Compatible module | Firmware evidence | Current status |
| --- | --- | --- |
| BLE frame header | `dtiot_ble_recv_data`, AP `0x103ce7b8`; owner-tested SDK | implemented |
| Token byte hash | AP `0x103c902e`, `hash = hash * 33 + byte` | implemented |
| Challenge/token derivation | AP `0x103ca86c`, `0x103ca9e0`; owner-tested SDK | implemented behind crypto HAL |
| DeviceSecret derivation | AP `0x103dc658`, duplicate `dt_get_device_secret_md5` at `0x103c3cd8`, MD5 wrapper `0x103d176c` | `hex(MD5(hex(hardware_uid_16)))` implemented and known-vector tested |
| Factory identity boundary | AP `0x103e9498`, `0x103e95a4`, `0x103e968c`, `0x103e9778`..`0x103e9c70` | 64-KiB DTIOT area and separate SN/MAC/name factory fields documented; target HAL pending |
| DTIOT unbind data | AP `0x103cf6c4`, `0x103cf954`, `0x103cf8f4` | random binding secret and fixed data slots documented; kept distinct from hardware-derived deviceSecret |
| Gray feature switches | AP `0x103cd7c8`; persistent keys `persist.dt.logrecord`, `persist.dt.aud.stream_rec`, `persist.dt.ble.param_auto`, `persist.dt.remark` | defaults, update semantics, remark value two and stream-record event translation modeled |
| Gray control actions | AP `0x103ca3f8`, response helper `0x103ca1b8` | key `1..5` action/order modeled as a non-executing plan; arbitrary shell command deliberately not exposed by owner SDK |
| Logical connect token check | AP `0x103ccb58`, string DID, 64-byte token, optional number/string timestamp, model and `sdk_ver` | typed session model and bounded request decoder implemented |
| Disconnect DID check | AP `0x103ccfc8` | typed session model implemented; request body is only required to be a valid object because stock checks the active session DID |
| Physical BLE link lifecycle | AP `0x103dba08`, `0x103dbe24`, counters `0x103db5ec`/`0x103db654`, cache lookup `0x103db864` | single-peer state/effects, disconnect cleanup and ADB advertising exception implemented and host-tested |
| Phone-specific BLE tuning | AP `0x103db90c`, `0x103dc3d4`, diagnostics `0x103dc514` | exact phone-family classifier and iPhone/iPad initial parameters implemented; unrecovered sleep-mode jump table kept out |
| Request command registry | AP `0x103ce7b8` switch | implemented |
| Unknown command response | AP `0x103ce7b8` fall-through sends code 200 | implemented |
| End-to-end command service | AP `0x103ce7b8` dispatch plus the command-specific handlers below | portable session/JSON/HAL orchestration implemented for 22 of 27 registry commands; provisioning, factory reset and OTA application remain isolated |
| Streaming BLE endpoint | AP `0x103cef04`, frame packer AP `0x103cf0ac` | arbitrary characteristic-write fragments, coalesced frames, direction/size gates and full response-frame encoding implemented and host-tested |
| System control key range | `dtiot_ble_system_control_proc`, AP `0x103cd510`; request parser `0x103cd64c` | keys `1..5`/`10001` and exact action ordering modeled as a non-executing plan |
| Two-button event policy | `dt_audio_control_button_callback` AP `0x103d7554`; `dt_button` AP `0x103d896c` | all event values `0..9`, timing thresholds, marker, pause/resume, live/memo routing, and cancellable shutdown modeled and host-tested |
| Display-state IDs | `dt_display_test_ui_states` AP `0x103e02c8`, tables at `0x104fff3c`/`0x104fff8c`; display thread `0x103e0598` | 20 diagnostic names plus charging state 37 modeled; malformed diagnostic entry 21 intentionally unnamed |
| Display polling/return policy | display thread AP `0x103e0598`; state mask `0x0fd92307`; content renderer `0x103ddc00` | 50/500/1000 ms polling intervals, 3-second transient return, activation-failure and charging targets modeled |
| BLE device identity | `get_device_info_callback` AP `0x103db2e8`; getters `0x103dc618`, `0x103dc640`, `0x103dc64c` | typed identity/capability model, response serializer and service handler implemented |
| BLE transport provisioning | AP `0x103cc598`; request `corpId`; encrypted plaintext fields `devkey`/`sn`/`secret` | response schema and authorization boundary documented; compatible core deliberately does not export factory credentials |
| BLE audio/device status | `audio_status` AP `0x103cc950`; state getter `0x103d7e50`, duration `0x103d6bf8`, FID `0x103d4898` | five-state names, millisecond duration, optional FID and status fields modeled |
| Storage status units | `dtiot_os_hal_get_storage_info` AP `0x103c85cc`; `statvfs` values shifted right by 20 | exact block-to-MiB conversion implemented and owner-observed 59,630/59,576 MiB fixture tested |
| USB device-info schema | USB command 400 in AP `0x103e34e8` | shared identity/status fields modeled; stock `battery_persent` typo retained only at adapter boundary |
| Ordinary recording actions | `dtiot_ble_bind_on_audio_request`, AP `0x103cc040`, events 0/3/1/2 and action strings | typed state model and request decoder implemented |
| Audio setting keys/ranges | AP `0x103cc040`, persistent-key call chain | all eight key/value forms decoded and validated atomically |
| Audio/gray persistence | AP `0x103cc040`, gray handler AP `0x103cd7c8`; recovered `persist.dt.*` keys | optional key/value HAL loads and stores exact persistent settings; live upload switch is intentionally not persisted; write failures restore service state |
| Protocol JSON boundary | AP cJSON call patterns across `0x103cb888`..`0x103ce138` | bounded depth-16 reader validates complete documents, arrays, strings/Unicode and signed/unsigned integers without an external dependency |
| Voiceprint request | AP `0x103cc464`, exact `start`/`stop` action strings | typed request decoder implemented; target audio binding pending |
| Live-stream start/stop | call chain from `dt_button` | platform boundary defined |
| Local voice memo | `dt_audio_start_voice_memo` `0x103d49b4`; stop `0x103d4d40` | platform boundary defined |
| Voice-memo aggregate format | stock `/emmc/audio/00000000000000`; start/stop/flush chain and owner sample | header codecs implemented |
| Internal vibration | high-level wrapper AP `0x103c7d84` | platform boundary defined |
| BLE vibration command | absent from the recovered V1.6.88 dispatch graph | intentionally not exposed |
| Raw file read | AP `0x103e4bd0`, `0x103e477c`, `0x103ca73c` | 8000-byte block codec and offset/sequence state implemented |
| Raw block CRC | AP `0x103e57a0`, `0x103e57f4`, polynomial literal `0x04c11db7` | CRC-32/BZIP2 implemented |
| Raw block ACK/retry | AP `0x103e4964`, `0x103e5060`; code `0x0193`, max 3 retries | state model implemented |
| Ordinary recording index | owner-tested SDK and `0x0110` firmware path; `code:u16be`, `count:u16be`, 8-byte entries | decoder implemented and host-tested |
| Persistent audio database | AP `0x103d241a`..`0x103d2950`, add/update/query/delete `0x103d3074`..`0x103d3f54` | eight-byte header/record codecs, 40-bit FID, tombstones, segment geometry and compaction policy implemented and host-tested |
| Recording lifecycle policy | AP `0x103d51d4`, `0x103d54fc`, `0x103d5b58`, `0x103d66a0` | 1,024-byte flush, 80-byte header patch, 2,048/4,800-byte deletion boundaries, 1.2/5-second timing and storage thresholds implemented and host-tested |
| Recording-list policy/trailer | AP `0x103cb4a4`, `0x103cb554`, list worker `0x103e17b0` | SDK-1.6.88/force-sync policy and `000000<truncated>ZZZZ` trailer encode/decode modeled |
| Ordinary file setup | AP `0x103cb888`, `0x103e1df4`, `0x103e2104`; fields `fid`, `offset`, `progress`; path `/emmc/audio/%014llu` | typed request decoder plus response codes, idle/paused gate and 48,000-byte slice documented |
| Ordinary file block | AP workers `0x103e1374`, `0x103e1688`; owner-tested `0x0115` path | 16-byte BE header, data, 8-byte reserved/CRC trailer, receive order and three-retry sender modeled and host-tested |
| File cancel/delete | AP `0x103cba0c`, `0x103cba84`, `0x103e19fc`, `0x103e1a78` | FID request decoder implemented; asynchronous cancel and response-before-delete ordering documented; owner SDK exposes confirmed single-record deletion |
| USB HID transport | AP `0x103e2970`, `0x103e2db0`, `0x103e34e8`; owner-tested VID:PID `17ef:0101`, report ID 1, 127 payload bytes | little-endian frame, 1,024-byte cap, five-second whole-frame timeout and HID segmentation/reassembly implemented and host-tested |
| Live audio blocks | owner-tested SDK and `0x0117`; fid at 4, sequence at 16, data length at 20, 84-byte units at 28 | decoder implemented and host-tested |
| Device-side live stream transport | AP `0x103c6684`, `0x103c688c`, packer `0x103c6b04`, sender `0x103c6c4c`, init `0x103c6d60`, stats `0x103c6ab0` | queue limits, batching, exact 24-byte header/eight-byte trailer, drop counter and boundary types implemented and host-tested |
| Marker/remark round trip | button AP `0x103d896c`; debounce `0x103d9c84`/`0x103d9cc8`; frame path `0x103d6a78`/`0x103d9d84`; BLE send `0x103d9fac`; app feedback `0x103ce138` | three-second debounce, pending timestamp, type-0 event, and type-2 feedback validation modeled |
| Batched telemetry | owner-tested SDK and `0x000c`; `ZZ` envelope and 20-byte records | conservative record/marker decoder implemented and host-tested |
| DTYJ recording container | owner-tested recordings; `BABA`, length, `DTYJ`, RIFF-like `fmt `/`data` chunks | zero-copy parser implemented and host-tested |
| Opus/Ogg export | RFC 6716 TOC duration and Ogg framing used by owner-tested SDK | packet duration and Ogg page codec implemented and host-tested |
| Schedule request and validation | AP `0x103cdc30`; helpers `0x103da090`, `0x103da0b4`, `0x103da0fc` | set/get JSON, stock 20-entry truncation, interval validation, unexpired and next-start selection modeled |
| Schedule persistence | AP `0x103da214`, `0x103da500`; raw Thumb around M55 `DLS`/`LE` loops | native-LE `SCHE` v1 header, 24-byte records and additive byte checksum implemented and host-tested |
| Firmware version query | AP `0x103ce610`, parser AP `0x103c5c68`, resume lookup `0x103c5d20` | `new_ver`, component comparison, `upgrade`/`cur_ver`/optional `offset`, query gate state and resume-offset HAL implemented; inbound OTA remains isolated |
| Schedule runtime orchestration | AP `0x103da3c8`, `0x103da7d0`, `0x103dae20` | 120-second precise/coarse timer policy modeled; exact overlapping-interval recording lifecycle remains under review |
| Wi-Fi AP/HTTP | AP `0x103cd098`, `0x103c8290`, `0x103c83fc`; request field `type`; web listen uses network-order `0x5000` | request decoder plus AP lifecycle and port-80 model implemented behind HAL |
| Wi-Fi TCP transfer | AP `0x103c81f8`, `0x103e516c`, `0x103e5286`, `0x103e5514`, `0x103e5574`; port `0x1722` (5922), handlers `0x0114/0x0115` | stream decoder, 40-KiB cap, kind/command policy and lifecycle modeled; socket adapter pending |
| Wi-Fi TCP OTA block | AP `0x103e7088`, CRC wrapper `0x103e583c` | type-zero gate, CRC/sequence/length fields and payload codec implemented and host-tested |
| Raw known-path BLE read | AP `0x103e4bd0`, fields `path`/optional `offset`, workers `0x103e477c`/`0x103e4964`, response `0x103e5060`; commands `0x014A..0x014C` | bounded 63-byte path request decoder and 8,000-byte block/CRC sender modeled; Python streaming reader implemented and host-tested, hardware retest pending |
| Inbound OTA/user image | AP `0x103cbb78`, `0x103cbe58`, OTA state AP `0x103c5fcc`/`0x103c60f4`, TCP AP `0x103e6e4c`/`0x103e7088`, user image AP `0x103e22cc`/`0x103e25cc` | header/error mapping, OTA query gate, resume offset, binary block CRC/type/length validation, bounded accounting and final hash check implemented behind a staging-only HAL; no install/flash callback exists |
| NOR partition geometry | `bes_partition_init` AP `0x1026f1c8`; table AP `0x10497fd4`; owner readback `/dev/*` sizes | exact 12-entry, 16-MiB read-only layout model with overlap/bounds validation |
| Program-image headers | common boot check AP `0x10231e6c`; HiFi loader AP `0x10232354`; owner AP/APC1 readback differs from OTA only at first word | OTA/installed marker states, common magic and V1.6.88 HiFi entry/IRAM/DRAM map decoders implemented |
| Companion-core/RPTUN manifest | AP ops tables `0x10486488`/`0x104864c0`/`0x104864f8`; AP adapters `0x101c606c..0x101c6c70`; NuttX RPTUN `0x10260c44`; APC1 mirror config `0x10c225c4` | DSPC0/BTHC0/M55C1 names, boot/IPC IDs, local arena addresses, exact `0x10e0` arena sizing and `0xd0:+0x94` resource selector modeled read-only |
| BES RMT IPC channel behavior | AP `0x1024e6c8` open; AP `0x1024eb68` close; AP `0x1024ec38` start RX; AP `0x1024ecb0` stop RX; AP `0x10233974` send; AP `0x1024e470` RX ISR core; AP `0x1024e5d8` TX-complete ISR core | portable send-slot/pending-list lifecycle, borrowed-buffer ownership, RX partial-consume flow control, manual receive completion, IRQ enable/suspend/resume and busy transitions implemented and host-tested |
| M55C1 RMT IPC board callbacks | AP `0x10241c6c`; AP `0x10241c8a`; AP `0x10241d18`; AP `0x10241cba`; AP `0x10241cd2`; AP `0x10241d38`; AP `0x10241ca2`; AP `0x10241d0c`; AP `0x10241d00` | exact stock callback addresses and RX 88 / TX 91 IRQ roles captured in the read-only manifest; MMIO port not yet enabled |
| DSPC0 RMT IPC board callbacks | AP `0x10241d58`; AP `0x10241d72`; AP `0x10241df4`; AP `0x10241d9e`; AP `0x10241db2`; AP `0x10241e10`; AP `0x10241d86`; AP `0x10241de8`; AP `0x10241ddc` | exact stock callback addresses and RX 76 / TX 79 IRQ roles captured in the read-only manifest; MMIO port not yet enabled |
| BTHC0 RMT IPC board callbacks | AP `0x10241f76`; AP `0x10241fae`; AP `0x10242086`; AP `0x10242058`; AP `0x10241fda`; AP `0x1024203c`; AP `0x10241fc6`; AP `0x10242010`; AP `0x10242004` | exact stock callback addresses and RX 96 / TX 94 IRQ roles captured in the read-only manifest; dual-CMU MMIO port not yet enabled |

The stock raw-file send worker clears its retry field while preparing every
block. That appears to make repeated CRC retries unbounded even though the
resend worker checks a limit of three. The compatible raw state model preserves
the counter and aborts after three consecutive failures. This is a documented
reliability correction. The ordinary 48,000-byte recording sender has a separate
retry path and does preserve its three-attempt limit.

## Interpretation rules

- A generated Ghidra function is evidence, not source code.
- Names inferred from diagnostic strings remain marked as inferred until callers,
  arguments, and side effects agree.
- A stock command returning `code=200` is not proof that an action occurred.
- Destructive branches remain unimplemented until recovery and persistent-state
  boundaries are known.
- Product behavior that belongs in the phone/agent (for example development,
  research, or inspiration modes) does not get embedded into this core.
