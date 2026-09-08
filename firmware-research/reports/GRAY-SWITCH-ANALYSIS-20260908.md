# DingTalk A1 V1.6.88 gray-switch analysis

Command `0x0137` first parses JSON and performs the normal connected-DID check.
It supports actions `set`, `get`, and `control`.

## Feature switches

The `set`/`get` paths expose four values:

| JSON field | Persistent key / target | Stock default |
| --- | --- | ---: |
| `log_record` | `persist.dt.logrecord` | 0 |
| `stream_record` | `persist.dt.aud.stream_rec`, via audio event key 10 | 1 |
| `ble_conn_param_auto` | `persist.dt.ble.param_auto` | 0 |
| `remark` | `persist.dt.remark` | 0 |

For `stream_record`, the gray handler translates enabled to event value `1`
and disabled to event value `2`. Remark processing is actually enabled when
the stored value equals `2`, not merely when nonzero.

An unusual stock behavior is preserved in the compatible model: a `set` request
that omits `remark` writes zero to it, while omitted values for the other three
fields are left unchanged.

## Privileged control keys

The `control` object contains numeric `key` and optional string `val`. The
handler at AP `0x103ca3f8` maps:

| Key | Confirmed effect | Response order |
| ---: | --- | --- |
| 1 | save diagnostics and reboot | response first |
| 2 | execute fixed shell command `shutdown` | response first |
| 3 | execute caller-provided shell command from `val` | response first |
| 4 | acknowledgement only in this handler | response only |
| 5 | delete the oldest audio file | deletion first |

The response helper builds `{"key": <key>, "code": 0}` and records protocol
status 200. Key 3 is effectively an authenticated NSH command interface. It is
valuable evidence for laboratory recovery and dynamic validation, but it is a
dangerous product backdoor: a malformed or destructive command can erase state
or prevent boot. Therefore:

- `compat-src` represents the behavior only as a non-executing action plan;
- `dingtalk-a1-sdk` keeps arbitrary system commands explicitly unexposed;
- any future recovery-only tool must require an owner credential, an explicit
  unsafe opt-in, an allowlist, and a serial/USB recovery route.

This branch does not by itself prove that launching `adbd &` is sufficient to
switch the USB gadget from HID to ADB. The stock HID worker starts that command
as part of a larger authenticated mode transition, so real USB enumeration must
remain the deciding evidence.
