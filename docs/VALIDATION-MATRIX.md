# Upstream findings and independent validation

This page separates claims in the upstream research from behavior reproduced
by this toolkit. It avoids publishing credentials, device identifiers, BLE
addresses, recording IDs, or recording content.

## Sources pinned for reproducibility

- Upstream `main`: `3b8b09165f91a7b2e42dae6942931b3d62b21980`
- Upstream `findings/h5-and-processing-architecture`:
  `62cbbf24bfc57badafb16cbc857e51ad8174ea5f`
- Original repository: <https://github.com/AwHsR15/dingtalk-a1-reverse>

The findings branch is available as a pinned Git submodule. It has no license
grant at the pinned revision, so its prose and scripts are not copied or
relicensed here. The code in this toolkit is an independent implementation.

## Validation matrix

| Area | Status | Independent evidence |
|---|---|---|
| FE3C BLE discovery and GATT connection | Confirmed live | A named A1 was discovered and connected from Windows. |
| 8-byte frame header, big-endian command and length | Confirmed live + unit test | Fragmented notifications reassemble into command frames. |
| `0x0008` random challenge | Confirmed live | A 32-character challenge was returned. |
| AES-128-CBC token and `0x0133` authentication | Confirmed live | The owned device returned `code: 200` and capability flags. |
| Per-device nature of `deviceSecret` | Confirmed in an earlier controlled test | Credentials from one owned A1 were rejected by a second A1 with `code: 501` in two attempts. This is evidence against reuse, not a proof of server-side generation details. |
| `0x0132` status | Confirmed live | Idle state, battery, storage, and firmware fields were read. |
| `0x0110` file index: 4-byte header + 8-byte records | Confirmed live | Three records parsed; the remaining bytes were only `00`/`5A` padding. |
| `fid` resembles a Unix timestamp | Confirmed on observed records | Parser now rejects implausible values to detect layout drift. |
| `0x0111` / `0x0114` / `0x0115` read-only download and ACK sequence | Confirmed live | One recording downloaded; announced size and received size matched exactly. |
| DTYJ `BABA` container and fixed-size Opus records | Confirmed live | The downloaded file had `BABA` magic and 276 fixed-size records. |
| DTYJ frame flags | Confirmed live + regression test | A real file used `0x20` on one frame; `0x00`, `0x20`, and `0xC0` high-bit flags are accepted while low reserved bits are rejected. |
| DTYJ to Ogg/Opus conversion | Confirmed live | The newly downloaded 276-frame sample converted to a 5.52-second Ogg after adding support for the observed `0x20` flag. |
| `0x0137` gray-switch `get` | Confirmed live | A read-only request returned the expected setting fields. |
| `0x013F` battery query | Confirmed live | The owned device returned `code: 200`; battery percentage remains available in the status response. |
| `0x0100` `upload_stream` enable/disable | Confirmed live | Both commands returned `code: 200`; the bounded probe confirmed disable in `finally`. The DID added by the official client's common request wrapper is required. |
| `0x0117` Opus pushes | Confirmed live during recording | A three-second metadata-only probe received 151 pushes containing 153 Opus units. Most audio areas were 84 bytes; one was a 252-byte batch of three units. TOCs were `0x48`/`0x4B`, every push had four trailing padding bytes, no audio was saved, and disable returned `code:200`. This shows the upstream “fixed 84-byte audio per push” description is common but not universal. |
| H5 JSAPI and ASR contract inventory | Static scanner implemented | The scanner reports contract names and URL hosts without reproducing source snippets. A local H5 bundle is still needed for independent evidence. |
| `0x0113` single-file delete | Confirmed live on one owned device + guarded implementation | The decompiled helper uses string fields `did` and `fid`. One request returned `code:200` and the next read showed one fewer indexed recording; existing local backup files remained. The console requires the fid to be in the current device index and an exact typed confirmation; unbacked files require the stronger `DELETE-NOBACKUP-<fid>` value. Automated tests mock the device and never send this command. |
| Reset, unbind, OTA, firmware write | Intentionally not tested | Destructive or high-risk operations are outside this toolkit. |
| Voiceprint, AI key, schedule, Wi-Fi transfer, cloud ASR | Not independently validated | Upstream notes are leads, not claims made by this toolkit. |

## Reproduction commands

```powershell
python tools\a1_auth_test.py --config .a1-device.json --inspect
python tools\a1_live_stream_probe.py --config .a1-device.json --seconds 3
python tools\h5_contract_scan.py C:\private\path\to\h5-package.tar --json
python -m unittest discover -s tests -v
```

The local config and all downloaded recordings are ignored by Git. Never add
them manually to a commit or issue attachment.
