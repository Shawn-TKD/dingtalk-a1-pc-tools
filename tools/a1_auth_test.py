"""Minimal, non-destructive DingTalk A1 BLE authentication test."""

import argparse
import asyncio
import json
import os
from pathlib import Path
import time

from bleak import BleakClient, BleakScanner
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


A1_SERVICE = "0000fe3c-0000-1000-8000-00805f9b34fb"
COMMAND_CHAR = "0000fe1c-0000-1000-8000-00805f9b34fb"
NOTIFY_CHAR = "0000fe1b-0000-1000-8000-00805f9b34fb"


def make_frame(command: int, sequence: int, body: dict, frame_type: int = 0x13) -> bytes:
    payload = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode()
    return (
        bytes([frame_type])
        + command.to_bytes(2, "big")
        + bytes([sequence & 0xFF])
        + len(payload).to_bytes(4, "big")
        + payload
    )


def make_token(device_secret: str, challenge: str) -> str:
    if len(device_secret) < 16:
        raise ValueError("deviceSecret must contain at least 16 characters")
    plain = challenge.encode("ascii")
    if len(plain) % 16:
        raise ValueError("challenge length is not an AES block multiple")
    key = device_secret[:16].encode("ascii")
    encryptor = Cipher(algorithms.AES(key), modes.CBC(key)).encryptor()
    return (encryptor.update(plain) + encryptor.finalize()).hex()


class FrameReceiver:
    def __init__(self) -> None:
        self.buffer = bytearray()
        self.queue: asyncio.Queue[tuple[int, int, bytes]] = asyncio.Queue()

    def on_notification(self, _sender, data: bytearray) -> None:
        self.buffer.extend(data)
        while len(self.buffer) >= 8:
            if self.buffer[0] not in (0x31, 0x13, 0x14):
                del self.buffer[0]
                continue
            payload_length = int.from_bytes(self.buffer[4:8], "big")
            total_length = 8 + payload_length
            if len(self.buffer) < total_length:
                return
            frame = bytes(self.buffer[:total_length])
            del self.buffer[:total_length]
            command = int.from_bytes(frame[1:3], "big")
            self.queue.put_nowait((command, frame[3], frame[8:]))

    async def wait_frame(self, command: int, timeout: float) -> tuple[int, bytes]:
        async with asyncio.timeout(timeout):
            while True:
                received_command, sequence, payload = await self.queue.get()
                if received_command == command:
                    return sequence, payload

    async def wait_payload(self, command: int, timeout: float) -> bytes:
        _sequence, payload = await self.wait_frame(command, timeout)
        return payload

    async def wait_json(self, command: int, timeout: float) -> dict:
        payload = await self.wait_payload(command, timeout)
        return json.loads(payload.decode("utf-8"))


def summarize_file_index(payload: bytes) -> dict:
    summary = {"length": len(payload), "hex": payload.hex()}
    if len(payload) >= 8:
        summary["code_be16"] = int.from_bytes(payload[0:2], "big")
        summary["declared_count_be16"] = int.from_bytes(payload[2:4], "big")
        summary["tail_hex"] = payload[-4:].hex()
    return summary


async def find_a1(timeout: float, address: str | None = None):
    discovered = await BleakScanner.discover(timeout=timeout, return_adv=True)
    candidates = []
    for device, advertisement in discovered.values():
        services = {uuid.lower() for uuid in advertisement.service_uuids}
        if A1_SERVICE in services:
            candidates.append((advertisement.rssi, device))
    if not candidates:
        raise RuntimeError("no DingTalk A1 advertisement found")
    if address:
        wanted = address.casefold()
        for _rssi, device in candidates:
            if device.address.casefold() == wanted:
                return device
        raise RuntimeError(f"requested A1 address not found: {address}")
    candidates.sort(key=lambda item: item[0], reverse=True)
    return candidates[0][1]


def apply_config(args):
    """Fill common CLI arguments from a local JSON config without printing secrets."""
    config = {}
    config_path = getattr(args, "config", None)
    if config_path:
        config = json.loads(Path(config_path).read_text(encoding="utf-8"))
    for argument, key, fallback in (
        ("did", "did", None),
        ("corp_id", "corp_id", None),
        ("model", "model", "Windows_A1_Client"),
        ("sdk_version", "sdk_version", "V2.1.3"),
        ("address", "address", None),
    ):
        current = getattr(args, argument, None)
        setattr(args, argument, current or config.get(key) or fallback)
    args.device_secret = (
        getattr(args, "device_secret", None)
        or os.environ.get("A1_DEVICE_SECRET", "")
        or str(config.get("device_secret", ""))
    )
    missing = [name for name in ("did", "corp_id") if not getattr(args, name)]
    if not args.device_secret:
        missing.append("device_secret")
    if missing:
        raise RuntimeError(
            "missing configuration fields: " + ", ".join(missing)
            + "; provide --config or explicit arguments/environment"
        )
    return args


async def authenticate(args) -> int:
    apply_config(args)
    secret = args.device_secret

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
        random_response = await receiver.wait_json(0x0008, args.command_timeout)
        challenge = str(random_response["random"])
        print(f"Challenge received: {challenge[:4]}...{challenge[-4:]}")

        timestamp = str(int(time.time()))
        connect_body = {
            "did": args.did,
            "model": args.model,
            "sdk_ver": args.sdk_version,
            "corp_id": args.corp_id,
            "token": make_token(secret, challenge),
            "timestamp": timestamp,
        }
        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(0x0133, 0x11, connect_body),
            response=True,
        )
        result = await receiver.wait_json(0x0133, args.command_timeout)
        print("Authentication response:", json.dumps(result, ensure_ascii=False, sort_keys=True))

        if result.get("code") == 200 and args.inspect:
            await client.write_gatt_char(
                COMMAND_CHAR,
                make_frame(0x0132, 0x12, {"did": args.did}),
                response=True,
            )
            status = await receiver.wait_json(0x0132, args.command_timeout)
            print("Device status:", json.dumps(status, ensure_ascii=False, sort_keys=True))

            await client.write_gatt_char(
                COMMAND_CHAR,
                make_frame(
                    0x0110,
                    0x13,
                    {"did": args.did, "s_fid": "0", "recently": 100, "e_fid": "0"},
                ),
                response=True,
            )
            file_index = await receiver.wait_payload(0x0110, args.command_timeout)
            print(
                "File index:",
                json.dumps(summarize_file_index(file_index), ensure_ascii=False, sort_keys=True),
            )

        await client.stop_notify(NOTIFY_CHAR)
        return 0 if result.get("code") == 200 else 4


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", help="local .a1-device.json path")
    parser.add_argument("--did")
    parser.add_argument("--corp-id")
    parser.add_argument("--model")
    parser.add_argument("--sdk-version")
    parser.add_argument("--address", help="optional BLE address; otherwise use strongest A1")
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--command-timeout", type=float, default=15.0)
    parser.add_argument(
        "--inspect",
        action="store_true",
        help="after authentication, read device status and the file index",
    )
    args = parser.parse_args()
    return asyncio.run(authenticate(args))


if __name__ == "__main__":
    raise SystemExit(main())
