"""Continuously receive A1 live Opus pushes and save voice memos as Ogg."""

from __future__ import annotations

import argparse
import asyncio
from dataclasses import dataclass, field
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
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
from dtyj_to_ogg import make_page
from siliconflow_asr import read_api_key, transcribe_sync, write_metadata


@dataclass
class Capture:
    fid: int
    started_at: float = field(default_factory=time.time)
    attributes: dict = field(default_factory=dict)
    packets: list[bytes] = field(default_factory=list)
    blocks: int = 0
    seen_blocks: set[tuple[int, bytes]] = field(default_factory=set)


def parse_json(payload: bytes) -> dict | None:
    try:
        value = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def parse_audio_push(payload: bytes) -> tuple[int, int, list[bytes]]:
    if len(payload) < 28:
        raise ValueError(f"short 0x0117 payload: {len(payload)} bytes")
    fid = int.from_bytes(payload[4:8], "big")
    block_sequence = int.from_bytes(payload[16:20], "big")
    audio_length = int.from_bytes(payload[20:24], "big")
    audio_end = 28 + audio_length
    if audio_length == 0 and audio_end <= len(payload):
        return fid, block_sequence, []
    if audio_length < 0 or audio_end > len(payload):
        raise ValueError(
            f"invalid 0x0117 audio length={audio_length}, payload={len(payload)}"
        )
    audio = bytes(payload[28:audio_end])
    if len(audio) % 84:
        raise ValueError(f"unexpected Opus area length: {len(audio)}")
    return fid, block_sequence, [audio[index:index + 84] for index in range(0, len(audio), 84)]


def make_ogg(packets: list[bytes], sample_rate: int, serial: int) -> bytes:
    if not packets:
        raise ValueError("cannot create an empty Ogg file")
    opus_head = (
        b"OpusHead" + bytes([1, 1]) + struct.pack("<H", 0)
        + struct.pack("<I", sample_rate) + struct.pack("<h", 0) + b"\x00"
    )
    vendor = b"a1_memo_capture/1"
    opus_tags = b"OpusTags" + struct.pack("<I", len(vendor)) + vendor + struct.pack("<I", 0)
    pages = [
        make_page(opus_head, 0x02, 0, serial, 0),
        make_page(opus_tags, 0x00, 0, serial, 1),
    ]
    granule = 0
    for index, packet in enumerate(packets):
        granule += 960
        pages.append(
            make_page(
                packet,
                0x04 if index == len(packets) - 1 else 0x00,
                granule,
                serial,
                index + 2,
            )
        )
    return b"".join(pages)


def sample_rate_from_attributes(attributes: dict) -> int:
    # Observed value: audio@opus@32000@84@1@16@32000@4
    value = str(attributes.get("attrs", ""))
    parts = value.split("@")
    if len(parts) > 2:
        try:
            rate = int(parts[2])
            if 8000 <= rate <= 192000:
                return rate
        except ValueError:
            pass
    return 32000


async def save_capture(capture: Capture, output_dir: Path, args) -> None:
    sample_rate = sample_rate_from_attributes(capture.attributes)
    output_dir.mkdir(parents=True, exist_ok=True)
    ogg_path = output_dir / f"memo-{capture.fid}.ogg"
    metadata_path = output_dir / f"memo-{capture.fid}.json"
    temporary = ogg_path.with_suffix(".ogg.tmp")
    temporary.write_bytes(make_ogg(capture.packets, sample_rate, capture.fid & 0xFFFFFFFF))
    temporary.replace(ogg_path)
    metadata = {
        "kind": "voice_memo",
        "fid": capture.fid,
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "duration_seconds": round(len(capture.packets) * 0.02, 2),
        "packet_count": len(capture.packets),
        "block_count": capture.blocks,
        "sample_rate": sample_rate,
        "stream_type": capture.attributes.get("stream_type"),
        "transcription": None,
        "transcription_error": None,
    }
    write_metadata(metadata_path, metadata)
    print(
        f"SAVED fid={capture.fid} duration={metadata['duration_seconds']:.2f}s "
        f"packets={len(capture.packets)} file={ogg_path}",
        flush=True,
    )
    # The web console keeps a user-entered key in process memory. The CLI still
    # supports its environment/file lookup without duplicating either path.
    api_key = getattr(args, "api_key", "") or read_api_key(args.api_key_file)
    if not args.transcribe or not api_key:
        return
    try:
        text = await asyncio.to_thread(transcribe_sync, ogg_path, api_key, args.asr_model)
        metadata["transcription"] = text
        print(f"TRANSCRIBED fid={capture.fid} text={text}", flush=True)
    except Exception as error:
        metadata["transcription_error"] = str(error)
        print(f"TRANSCRIPTION_FAILED fid={capture.fid} error={error}", flush=True)
    write_metadata(metadata_path, metadata)


async def authenticate(client: BleakClient, receiver: FrameReceiver, args) -> None:
    await client.start_notify(NOTIFY_CHAR, receiver.on_notification)
    await client.write_gatt_char(
        COMMAND_CHAR,
        make_frame(0x0008, 0x60, {"did": args.did, "corp_id": args.corp_id}),
        response=True,
    )
    challenge = str((await receiver.wait_json(0x0008, args.command_timeout))["random"])
    await client.write_gatt_char(
        COMMAND_CHAR,
        make_frame(
            0x0133,
            0x61,
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
    result = await receiver.wait_json(0x0133, args.command_timeout)
    if result.get("code") != 200:
        raise RuntimeError(f"device rejected authentication: code={result.get('code')}")


async def listen_once(args) -> None:
    device = await find_a1(args.scan_timeout, args.address)
    receiver = FrameReceiver()
    async with BleakClient(device, timeout=args.command_timeout) as client:
        await authenticate(client, receiver, args)
        print(f"READY address={device.address}; short-press A1 to record a voice memo", flush=True)
        current: Capture | None = None
        while client.is_connected:
            command, _sequence, payload = await receiver.queue.get()
            if command == 0x0100:
                body = parse_json(payload)
                if not body:
                    continue
                action = body.get("action")
                if action == "start":
                    fid = int(body.get("fid") or int(time.time()))
                    if current and current.packets:
                        await save_capture(current, args.output_dir, args)
                    current = Capture(fid=fid)
                    print(f"RECORDING fid={fid}", flush=True)
                elif action == "stop" and current:
                    completed, current = current, None
                    if completed.packets:
                        await save_capture(completed, args.output_dir, args)
                    else:
                        print(f"EMPTY fid={completed.fid}; no audio frames received", flush=True)
            elif command == 0x0116:
                body = parse_json(payload)
                if body:
                    fid = int(body.get("fid") or (current.fid if current else 0))
                    if current and (not fid or fid == current.fid):
                        current.attributes.update(body)
                        print(
                            f"ATTRIBUTES fid={current.fid} stream_type={body.get('stream_type')} "
                            f"attrs={body.get('attrs')}",
                            flush=True,
                        )
            elif command == 0x0117:
                try:
                    fid, block_sequence, packets = parse_audio_push(payload)
                except ValueError as error:
                    print(f"IGNORED malformed audio push: {error}", flush=True)
                    continue
                if not packets:
                    continue
                if current is None:
                    current = Capture(fid=fid)
                    print(f"RECORDING fid={fid} (inferred from audio stream)", flush=True)
                if fid != current.fid:
                    continue
                signature = (block_sequence, b"".join(packets))
                if signature in current.seen_blocks:
                    continue
                current.seen_blocks.add(signature)
                current.blocks += 1
                current.packets.extend(packets)


async def run(args) -> int:
    apply_config(args)
    delay = 1.0
    while True:
        try:
            await listen_once(args)
            delay = 1.0
        except asyncio.CancelledError:
            raise
        except KeyboardInterrupt:
            return 0
        except Exception as error:
            print(f"DISCONNECTED {type(error).__name__}: {error}; retrying in {delay:.0f}s", flush=True)
            if args.once:
                return 2
            await asyncio.sleep(delay)
            delay = min(delay * 2, 15.0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=".a1-device.json")
    parser.add_argument("--did")
    parser.add_argument("--corp-id")
    parser.add_argument("--device-secret")
    parser.add_argument("--model")
    parser.add_argument("--sdk-version")
    parser.add_argument("--address")
    parser.add_argument("--output-dir", type=Path, default=Path("recordings"))
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--command-timeout", type=float, default=15.0)
    parser.add_argument("--transcribe", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--api-key-file", type=Path, default=Path(".siliconflow-api-key"))
    parser.add_argument("--asr-model", default="FunAudioLLM/SenseVoiceSmall")
    parser.add_argument("--once", action="store_true", help="exit instead of reconnecting after an error")
    return asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    raise SystemExit(main())
