# DingTalk A1 V1.6.88 scheduled-recording reconstruction

This note records confirmed firmware evidence. Generated pseudocode is not
treated as original source.

## BLE command `0x011a`

The request handler at AP `0x103cdc30` accepts an `action` string:

- `set`: optional numeric `current`, plus a `params` array;
- `get`: returns the stored schedule and current device clock.

Each array item contains 64-bit numeric `start` and `end` values and an optional
numeric `sid`. The request is capped at 20 items. A set succeeds only when every
processed item is valid: `start != 0`, `start < end`, and `current < end`.
The handler does not partially accept a mixed valid/invalid array. An empty or
missing `params` list clears schedule state and removes its persisted file.

When the supplied `current` is above `0x696e5500` and the platform exposes a
clock setter, the stock handler also updates the device clock. This boundary is
2026-01-19 16:00:00 UTC (2026-01-20 in China Standard Time).

## Persistent format

Path: `/emmc/schedule/schedule.dat`

All values are written in the AP's native little-endian order.

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `SCHE` (`0x45484353`) |
| 4 | 4 | format version, exactly `1` |
| 8 | 8 | request/device `current` timestamp |
| 16 | 4 | entry count, `1..20` in a persisted file |
| 20 | 4 | unsigned sum of every byte in the entry area |
| 24 | `count * 24` | raw schedule entries |

Each entry is 24 bytes:

| Entry offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 8 | start timestamp |
| 8 | 8 | end timestamp |
| 16 | 2 | schedule id (`sid`) |
| 18 | 6 | reserved/padding |

The checksum loop was initially rendered by Ghidra as an infinite `loopEnd()`.
Raw Thumb bytes show Cortex-M55 low-overhead-loop instructions (`DLS`/`LE`):
the body is one byte load and a wrapping 32-bit addition. Load compares that sum
with the header field; save calculates the same sum before writing.

## Runtime helpers

- `0x103da090`: interval valid iff start is nonzero and strictly before end.
- `0x103da0b4`: detects any valid interval whose end is strictly after now.
- `0x103da0fc`: selects the smallest valid start strictly after now.
- the timer switches to its precise path when the next start is at most 120
  seconds away; otherwise it uses the coarse schedule timer.
- `0x103da7d0` merges overlapping active intervals, starts/stops scheduled
  recording, and preserves the selected `sid`. Its complete clean-room state
  machine is still being reconstructed; it is not silently guessed in the
  portable core.

## Compatible implementation

`compat-src/include/a1/schedule.h` and `compat-src/src/schedule.c` implement the
confirmed portable behavior. They do not touch the real clock or filesystem;
those actions remain future HAL responsibilities.
