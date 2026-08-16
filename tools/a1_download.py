"""Download one recording from an owned DingTalk A1 without deleting it."""

import argparse
import asyncio
import hashlib
import json
import os
from pathlib import Path
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


def json_body(payload: bytes) -> dict:
    return json.loads(payload.decode("utf-8"))


def find_size(value) -> int | None:
    if isinstance(value, dict):
        for key in ("size", "file_size", "fileSize", "total_size", "totalSize"):
            candidate = value.get(key)
            if isinstance(candidate, (int, str)) and str(candidate).isdigit():
                return int(candidate)
        for child in value.values():
            result = find_size(child)
            if result is not None:
                return result
    elif isinstance(value, list):
        for child in value:
            result = find_size(child)
            if result is not None:
                return result
    return None


async def download(args) -> int:
    apply_config(args)
    secret = args.device_secret
    output = Path(args.output)
    if output.exists():
        raise FileExistsError(f"refusing to overwrite {output}")

    device = await find_a1(args.scan_timeout, args.address)
    print(f"Connecting to {device.name!r} at {device.address}")
    receiver = FrameReceiver()
    async with BleakClient(device, timeout=args.command_timeout) as client:
        await client.start_notify(NOTIFY_CHAR, receiver.on_notification)

        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(0x0008, 0x10, {"did": args.did, "corp_id": args.corp_id}),
            response=True,
        )
        challenge = str((await receiver.wait_json(0x0008, args.command_timeout))["random"])
        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(
                0x0133,
                0x11,
                {
                    "did": args.did,
                    "model": args.model,
                    "sdk_ver": args.sdk_version,
                    "corp_id": args.corp_id,
                    "token": make_token(secret, challenge),
                    "timestamp": str(int(time.time())),
                },
            ),
            response=True,
        )
        auth = await receiver.wait_json(0x0133, args.command_timeout)
        if auth.get("code") != 200:
            raise RuntimeError(f"authentication rejected: {auth}")
        print("Authenticated")

        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(
                0x0111,
                0x12,
                {"did": args.did, "fid": str(args.fid), "offset": 0, "progress": 65537},
            ),
            response=True,
        )
        accepted = await receiver.wait_json(0x0111, args.command_timeout)
        if accepted.get("code") not in (200, 202):
            raise RuntimeError(f"download rejected: {accepted}")
        print("Download request:", json.dumps(accepted, sort_keys=True))

        attribute_sequence, attribute_payload = await receiver.wait_frame(
            0x0114, args.transfer_timeout
        )
        try:
            attributes = json_body(attribute_payload)
            print("File attributes:", json.dumps(attributes, ensure_ascii=False, sort_keys=True))
        except (UnicodeDecodeError, json.JSONDecodeError):
            attributes = {"raw_hex": attribute_payload.hex()}
            print("File attributes (binary):", attributes["raw_hex"])
        expected_size = find_size(attributes)
        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(0x0114, attribute_sequence, {"code": 200}, frame_type=0x31),
            response=True,
        )

        chunks: dict[int, bytes] = {}
        received_size = 0
        while expected_size is None or received_size < expected_size:
            sequence, payload = await receiver.wait_frame(0x0115, args.transfer_timeout)
            if len(payload) < 16:
                raise RuntimeError(f"short file block: {len(payload)} bytes")
            payload_fid = int.from_bytes(payload[2:6], "big")
            block_number = int.from_bytes(payload[8:12], "big")
            block_length = int.from_bytes(payload[12:16], "big")
            block = payload[16 : 16 + block_length]
            if payload_fid != args.fid:
                raise RuntimeError(f"unexpected fid in block: {payload_fid}")
            if len(block) != block_length:
                raise RuntimeError(
                    f"short block {block_number}: declared={block_length} actual={len(block)}"
                )
            chunks[block_number] = block
            received_size = sum(len(value) for value in chunks.values())
            print(f"Block {block_number}: {block_length} bytes; total={received_size}")
            await client.write_gatt_char(
                COMMAND_CHAR,
                make_frame(0x0115, sequence, {"code": 200}, frame_type=0x31),
                response=True,
            )
            if expected_size is None and block_length < 48000:
                break

        content = b"".join(chunks[number] for number in sorted(chunks))
        if expected_size is not None and len(content) != expected_size:
            raise RuntimeError(f"size mismatch: expected={expected_size} actual={len(content)}")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(content)
        print(
            f"Saved {len(content)} bytes to {output}; "
            f"sha256={hashlib.sha256(content).hexdigest()}; magic={content[:4]!r}"
        )
        await client.stop_notify(NOTIFY_CHAR)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", help="local .a1-device.json path")
    parser.add_argument("--did")
    parser.add_argument("--corp-id")
    parser.add_argument("--fid", required=True, type=int)
    parser.add_argument("--output", required=True)
    parser.add_argument("--model")
    parser.add_argument("--sdk-version")
    parser.add_argument("--address", help="optional BLE address; otherwise use strongest A1")
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--command-timeout", type=float, default=15.0)
    parser.add_argument("--transfer-timeout", type=float, default=90.0)
    args = parser.parse_args()
    return asyncio.run(download(args))


if __name__ == "__main__":
    raise SystemExit(main())
