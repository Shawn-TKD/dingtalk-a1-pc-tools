"""Briefly validate the A1 live Opus stream without storing audio content."""

from __future__ import annotations

import argparse
import asyncio
from collections import Counter
import json
import time

from bleak import BleakClient

from a1_auth_test import (
    COMMAND_CHAR,
    NOTIFY_CHAR,
    FrameReceiver,
    apply_config,
    find_a1,
    make_frame,
    make_token,
)


def parse_stream_metadata(payload: bytes) -> dict:
    """Return structural metadata only; deliberately omit the Opus bytes."""
    if len(payload) < 32:
        raise ValueError(f"short live-stream payload: {len(payload)} bytes")
    fid = int.from_bytes(payload[4:8], "big")
    block_sequence = int.from_bytes(payload[16:20], "big")
    audio_length = int.from_bytes(payload[20:24], "big")
    audio_end = 28 + audio_length
    if audio_length <= 0 or audio_end > len(payload):
        raise ValueError(
            f"invalid live-stream audio length={audio_length}, payload={len(payload)}"
        )
    trailing = payload[audio_end:]
    if any(byte not in (0x00, 0x5A) for byte in trailing):
        raise ValueError(f"unexpected live-stream tail ({len(trailing)} bytes)")
    # Most pushes contain one 84-byte Opus unit. A live sample also batched
    # three such units into a 252-byte audio area, so report each unit's TOC.
    unit_offsets = range(0, audio_length, 84) if audio_length % 84 == 0 else (0,)
    opus_tocs = [payload[28 + offset] for offset in unit_offsets]
    return {
        "fid": fid,
        "block_sequence": block_sequence,
        "audio_length": audio_length,
        "opus_tocs": opus_tocs,
        "opus_units": len(opus_tocs),
        "trailing_bytes": len(trailing),
    }


def stream_control_body(did: str, enabled: bool) -> dict:
    # The decompiled client builds the action/params map first, then its
    # common k(map) wrapper injects the current DID before serialization.
    return {
        "did": did,
        "action": "set",
        "params": [{"key": "upload_stream", "val": 1 if enabled else 0}],
    }


async def probe(args) -> int:
    apply_config(args)
    if not 1.0 <= args.seconds <= 15.0:
        raise ValueError("--seconds must be between 1 and 15")

    device = await find_a1(args.scan_timeout, args.address)
    print(f"Connecting to {device.name!r} at {device.address}")
    receiver = FrameReceiver()
    control_attempted = False
    async with BleakClient(device, timeout=args.command_timeout) as client:
        await client.start_notify(NOTIFY_CHAR, receiver.on_notification)
        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(0x0008, 0x20, {"did": args.did, "corp_id": args.corp_id}),
            response=True,
        )
        challenge = str((await receiver.wait_json(0x0008, args.command_timeout))["random"])
        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(
                0x0133,
                0x21,
                {
                    "did": args.did,
                    "model": args.model,
                    "sdk_ver": args.sdk_version,
                    "corp_id": args.corp_id,
                    "token": make_token(args.device_secret, challenge),
                    "timestamp": str(int(time.time())),
                },
            ),
            response=True,
        )
        auth = await receiver.wait_json(0x0133, args.command_timeout)
        if auth.get("code") != 200:
            raise RuntimeError(f"authentication rejected: {auth}")
        print("Authenticated; enabling metadata-only live-stream probe")

        samples: list[dict] = []
        stop_result = None
        try:
            control_attempted = True
            await client.write_gatt_char(
                COMMAND_CHAR,
                make_frame(
                    0x0100,
                    0x22,
                    stream_control_body(args.did, True),
                ),
                response=True,
            )
            start_result = await receiver.wait_json(0x0100, args.command_timeout)
            if start_result.get("code") != 200:
                raise RuntimeError(f"live-stream enable rejected: {start_result}")
            deadline = asyncio.get_running_loop().time() + args.seconds
            while len(samples) < args.max_frames:
                remaining = deadline - asyncio.get_running_loop().time()
                if remaining <= 0:
                    break
                try:
                    _sequence, payload = await receiver.wait_frame(0x0117, min(remaining, 1.0))
                except TimeoutError:
                    continue
                samples.append(parse_stream_metadata(payload))
        finally:
            if control_attempted and client.is_connected:
                try:
                    await client.write_gatt_char(
                        COMMAND_CHAR,
                        make_frame(
                            0x0100,
                            0x23,
                            stream_control_body(args.did, False),
                        ),
                        response=True,
                    )
                    try:
                        stop_result = await receiver.wait_json(0x0100, args.command_timeout)
                    except TimeoutError:
                        stop_result = {"code": "timeout"}
                except Exception as error:  # best-effort safety path during disconnects
                    stop_result = {"code": "error", "type": type(error).__name__}
            await client.stop_notify(NOTIFY_CHAR)

        summary = {
            "frames": len(samples),
            "opus_units": sum(item["opus_units"] for item in samples),
            "streams": len({item["fid"] for item in samples}),
            "first_block": samples[0]["block_sequence"] if samples else None,
            "last_block": samples[-1]["block_sequence"] if samples else None,
            "audio_lengths": dict(sorted(Counter(item["audio_length"] for item in samples).items())),
            "opus_toc": {
                f"0x{toc:02x}": count
                for toc, count in sorted(
                    Counter(toc for item in samples for toc in item["opus_tocs"]).items()
                )
            },
            "trailing_bytes": dict(
                sorted(Counter(item["trailing_bytes"] for item in samples).items())
            ),
            "audio_saved": False,
            "disable_response": stop_result,
        }
        print("Live-stream summary:", json.dumps(summary, ensure_ascii=False, sort_keys=True))
        if not samples:
            print("No 0x0117 frames arrived; the A1 may need to be actively recording.")
            return 5
        if not isinstance(stop_result, dict) or stop_result.get("code") != 200:
            raise RuntimeError(f"live-stream disable was not confirmed: {stop_result}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", help="local .a1-device.json path")
    parser.add_argument("--did")
    parser.add_argument("--corp-id")
    parser.add_argument("--device-secret")
    parser.add_argument("--model")
    parser.add_argument("--sdk-version")
    parser.add_argument("--address", help="optional BLE address; otherwise use strongest A1")
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--command-timeout", type=float, default=15.0)
    parser.add_argument("--seconds", type=float, default=3.0)
    parser.add_argument("--max-frames", type=int, default=1000)
    return asyncio.run(probe(parser.parse_args()))


if __name__ == "__main__":
    raise SystemExit(main())
