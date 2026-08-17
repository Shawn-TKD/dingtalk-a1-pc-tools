"""Token-protected local/LAN API and static server for the DingTalk A1 console."""

from __future__ import annotations

import argparse
import asyncio
from datetime import datetime
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import hmac
import json
import mimetypes
import os
from pathlib import Path
import socket
import sys
import threading
from types import SimpleNamespace
import time
from urllib.parse import parse_qs, quote, unquote, urlparse
import webbrowser


APP_DIR = Path(__file__).resolve().parent
ROOT_DIR = APP_DIR.parent
TOOLS_DIR = ROOT_DIR / "tools"
DIST_DIR = APP_DIR / "dist"
sys.path.insert(0, str(TOOLS_DIR))

from bleak import BleakClient  # noqa: E402
from a1_auth_test import (  # noqa: E402
    COMMAND_CHAR,
    NOTIFY_CHAR,
    FrameReceiver,
    apply_config,
    find_a1,
    make_frame,
    make_token,
    parse_file_index,
)
from a1_download import download as download_from_a1  # noqa: E402
from dtyj_to_ogg import convert as convert_dtyj, extract_packets_with_markers  # noqa: E402
from a1_memo_capture import (  # noqa: E402
    Capture,
    authenticate as authenticate_memo,
    parse_audio_push,
    parse_json as parse_memo_json,
    save_capture,
)
from siliconflow_asr import read_api_key, transcribe_sync, write_metadata  # noqa: E402
from siliconflow_summary import summarize_sync  # noqa: E402
from a1_live_stream_probe import stream_control_body  # noqa: E402


LAST_STATE: dict = {"connected": False, "device": None, "recordings": []}
OPTIONS = None
DEVICE_OPERATION_LOCK = threading.Lock()
STATE_LOCK = threading.RLock()
API_KEY_OVERRIDE = ""


def configured_api_key() -> str:
    key_file = getattr(OPTIONS, "api_key_file", None)
    return API_KEY_OVERRIDE or read_api_key(Path(key_file) if key_file else None)


def metadata_paths(fid: int) -> tuple[Path, Path]:
    output_dir = Path(OPTIONS.output_dir)
    for prefix in ("memo", "a1"):
        audio = output_dir / f"{prefix}-{fid}.ogg"
        if audio.exists():
            return audio, output_dir / f"{prefix}-{fid}.json"
    return output_dir / f"a1-{fid}.ogg", output_dir / f"a1-{fid}.json"


def load_metadata(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return value if isinstance(value, dict) else {}


def update_metadata(fid: int, **changes) -> dict:
    _audio, path = metadata_paths(fid)
    metadata = load_metadata(path)
    metadata.update(changes)
    write_metadata(path, metadata)
    return metadata


def parse_byte_range(value: str | None, total_size: int) -> tuple[int, int] | None:
    """Parse one HTTP bytes range and return inclusive start/end offsets."""
    if not value:
        return None
    if total_size <= 0 or not value.startswith("bytes=") or "," in value:
        raise ValueError("unsupported byte range")
    start_text, separator, end_text = value.removeprefix("bytes=").partition("-")
    if not separator or (not start_text and not end_text):
        raise ValueError("invalid byte range")
    if not start_text:
        suffix_length = int(end_text)
        if suffix_length <= 0:
            raise ValueError("invalid byte range suffix")
        start = max(0, total_size - suffix_length)
        return start, total_size - 1
    start = int(start_text)
    end = int(end_text) if end_text else total_size - 1
    if start < 0 or start >= total_size or end < start:
        raise ValueError("byte range is outside the file")
    return start, min(end, total_size - 1)


def configured_args():
    args = SimpleNamespace(
        config=OPTIONS.config,
        did=None,
        corp_id=None,
        model=None,
        sdk_version=None,
        address=OPTIONS.address,
        device_secret=None,
    )
    return apply_config(args)


def load_public_metadata() -> dict:
    config = json.loads(Path(OPTIONS.config).read_text(encoding="utf-8"))
    return {
        "serial_number": str(config.get("serial_number", "")),
        "device_id": config.get("device_id"),
    }


def local_file_info(fid: int) -> dict:
    output_dir = Path(OPTIONS.output_dir)
    ogg = output_dir / f"a1-{fid}.ogg"
    dtyj = output_dir / f"a1-{fid}.dtyj"
    metadata_path = output_dir / f"a1-{fid}.json"
    metadata = load_metadata(metadata_path)
    if dtyj.exists() and (
        not metadata.get("markers_scanned") or metadata.get("duration_seconds") is None
    ):
        try:
            packets, _sample_rate, _version, markers = extract_packets_with_markers(
                dtyj.read_bytes()
            )
            metadata.update(
                markers=markers,
                markers_scanned=True,
                duration_seconds=round(len(packets) * 0.02, 2),
            )
            write_metadata(metadata_path, metadata)
        except (OSError, ValueError):
            # Conversion surfaces malformed containers to the user. State
            # polling stays available even if marker backfill cannot parse one.
            pass
    return {
        "local_url": f"/recordings/{ogg.name}" if ogg.exists() else None,
        "local_ogg_bytes": ogg.stat().st_size if ogg.exists() else None,
        "local_dtyj_bytes": dtyj.stat().st_size if dtyj.exists() else None,
        "local_duration": metadata.get("duration_seconds"),
        "transcription": metadata.get("transcription"),
        "transcription_error": metadata.get("transcription_error"),
        "transcription_model": metadata.get("transcription_model"),
        "summary": metadata.get("summary"),
        "summary_error": metadata.get("summary_error"),
        "summary_model": metadata.get("summary_model"),
        "markers": metadata.get("markers", []),
    }


def local_memo_info(ogg: Path) -> dict:
    fid = int(ogg.stem.removeprefix("memo-"))
    metadata_path = ogg.with_suffix(".json")
    metadata = load_metadata(metadata_path)
    return {
        "fid": fid,
        "kind": "voice_memo",
        "duration_seconds": float(metadata.get("duration_seconds") or 0),
        "local_duration": float(metadata.get("duration_seconds") or 0),
        "on_device": False,
        "local_url": f"/recordings/{ogg.name}",
        "local_ogg_bytes": ogg.stat().st_size,
        "local_dtyj_bytes": None,
        "transcription": metadata.get("transcription"),
        "transcription_error": metadata.get("transcription_error"),
        "transcription_model": metadata.get("transcription_model"),
        "summary": metadata.get("summary"),
        "summary_error": metadata.get("summary_error"),
        "summary_model": metadata.get("summary_model"),
        "captured_at": metadata.get("captured_at"),
    }


def backup_exists(fid: int) -> bool:
    info = local_file_info(fid)
    return bool(info["local_ogg_bytes"] and info["local_dtyj_bytes"])


def delete_local_recording(fid: int, kind: str) -> list[str]:
    """Delete only explicitly named local artifacts; never touch the A1."""
    prefix = "memo" if kind == "voice_memo" else "a1"
    output_dir = Path(OPTIONS.output_dir)
    removed = []
    for suffix in (".dtyj", ".ogg", ".json"):
        target = output_dir / f"{prefix}-{fid}{suffix}"
        if target.exists():
            target.unlink()
            removed.append(target.name)
    if not removed:
        raise ValueError("这条录音没有可删除的本地副本")
    return removed


def delete_request_body(did: str, fid: int) -> dict:
    """Match the official sendFileDelete(String did, String fid) JSON contract."""
    return {"did": did, "fid": str(fid)}


def access_token_matches(expected: str, provided: str) -> bool:
    return not expected or hmac.compare_digest(provided, expected)


def acquire_device_operation_lock() -> bool:
    """Wait for a paused memo listener to release its BLE connection."""
    timeout = max(
        1.0,
        float(getattr(OPTIONS, "scan_timeout", 12.0))
        + float(getattr(OPTIONS, "command_timeout", 15.0))
        + 3,
    )
    return DEVICE_OPERATION_LOCK.acquire(timeout=timeout)


def merge_local_files(state: dict) -> dict:
    merged = json.loads(json.dumps(state))
    known_fids = set()
    for recording in merged.get("recordings", []):
        fid = int(recording["fid"])
        known_fids.add(fid)
        recording.setdefault("on_device", True)
        if recording.get("kind") == "voice_memo":
            memo_ogg = Path(OPTIONS.output_dir) / f"memo-{fid}.ogg"
            if memo_ogg.exists():
                recording.update(local_memo_info(memo_ogg))
        else:
            recording.update(local_file_info(fid))
    output_dir = Path(OPTIONS.output_dir)
    if output_dir.exists():
        for ogg in output_dir.glob("a1-*.ogg"):
            try:
                fid = int(ogg.stem.removeprefix("a1-"))
            except ValueError:
                continue
            if fid in known_fids:
                continue
            merged.setdefault("recordings", []).append(
                {
                    "fid": fid,
                    "duration_seconds": 0,
                    "on_device": False,
                    **local_file_info(fid),
                }
            )
        for ogg in output_dir.glob("memo-*.ogg"):
            try:
                memo = local_memo_info(ogg)
            except (OSError, ValueError):
                continue
            if memo["fid"] in known_fids:
                continue
            merged.setdefault("recordings", []).append(memo)
    return merged


def indexed_device_fids(state: dict) -> set[int]:
    return {
        int(item["fid"])
        for item in state.get("recordings", [])
        if item.get("on_device", True)
    }


async def inspect_a1() -> dict:
    credentials = configured_args()
    device = await find_a1(OPTIONS.scan_timeout, credentials.address)
    receiver = FrameReceiver()
    async with BleakClient(device, timeout=OPTIONS.command_timeout) as client:
        await client.start_notify(NOTIFY_CHAR, receiver.on_notification)
        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(0x0008, 0x10, {"did": credentials.did, "corp_id": credentials.corp_id}),
            response=True,
        )
        challenge = str((await receiver.wait_json(0x0008, OPTIONS.command_timeout))["random"])
        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(
                0x0133,
                0x11,
                {
                    "did": credentials.did,
                    "model": credentials.model,
                    "sdk_ver": credentials.sdk_version,
                    "corp_id": credentials.corp_id,
                    "token": make_token(credentials.device_secret, challenge),
                    "timestamp": str(int(datetime.now().timestamp())),
                },
            ),
            response=True,
        )
        auth = await receiver.wait_json(0x0133, OPTIONS.command_timeout)
        if auth.get("code") != 200:
            raise RuntimeError(f"device rejected authentication: {auth}")
        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(0x0132, 0x12, {"did": credentials.did}),
            response=True,
        )
        status = await receiver.wait_json(0x0132, OPTIONS.command_timeout)
        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(
                0x0110,
                0x13,
                {"did": credentials.did, "s_fid": "0", "recently": 100, "e_fid": "0"},
            ),
            response=True,
        )
        index_payload = await receiver.wait_payload(0x0110, OPTIONS.command_timeout)
        await client.stop_notify(NOTIFY_CHAR)

    metadata = load_public_metadata()
    return merge_local_files(
        {
            "connected": True,
            "device": {
                **status,
                **metadata,
                "address": device.address,
                "capabilities": {
                    key: value for key, value in auth.items() if key.startswith("cap_")
                },
            },
            "recordings": parse_file_index(index_payload)["records"],
        }
    )


async def delete_recording(fid: int) -> dict:
    """Delete one indexed recording after the caller has verified a local backup."""
    credentials = configured_args()
    device = await find_a1(OPTIONS.scan_timeout, credentials.address)
    receiver = FrameReceiver()
    async with BleakClient(device, timeout=OPTIONS.command_timeout) as client:
        await client.start_notify(NOTIFY_CHAR, receiver.on_notification)
        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(0x0008, 0x20, {"did": credentials.did, "corp_id": credentials.corp_id}),
            response=True,
        )
        challenge = str((await receiver.wait_json(0x0008, OPTIONS.command_timeout))["random"])
        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(
                0x0133,
                0x21,
                {
                    "did": credentials.did,
                    "model": credentials.model,
                    "sdk_ver": credentials.sdk_version,
                    "corp_id": credentials.corp_id,
                    "token": make_token(credentials.device_secret, challenge),
                    "timestamp": str(int(datetime.now().timestamp())),
                },
            ),
            response=True,
        )
        auth = await receiver.wait_json(0x0133, OPTIONS.command_timeout)
        if auth.get("code") != 200:
            raise RuntimeError(f"device rejected authentication: {auth}")
        await client.write_gatt_char(
            COMMAND_CHAR,
            make_frame(0x0113, 0x22, delete_request_body(credentials.did, fid)),
            response=True,
        )
        result = await receiver.wait_json(0x0113, OPTIONS.command_timeout)
        await client.stop_notify(NOTIFY_CHAR)
    if result.get("code") != 200:
        raise RuntimeError(f"device rejected file deletion: {result}")
    return result


def discover_lan_ipv4_addresses() -> list[str]:
    addresses = set()
    try:
        for entry in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            address = entry[4][0]
            if not address.startswith(("127.", "169.254.")):
                addresses.add(address)
    except OSError:
        pass
    return sorted(addresses)


async def download_recording(fid: int) -> None:
    credentials = configured_args()
    output_dir = Path(OPTIONS.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    dtyj = output_dir / f"a1-{fid}.dtyj"
    ogg = output_dir / f"a1-{fid}.ogg"
    if not dtyj.exists():
        await download_from_a1(
            SimpleNamespace(
                config=OPTIONS.config,
                did=credentials.did,
                corp_id=credentials.corp_id,
                fid=fid,
                output=str(dtyj),
                model=credentials.model,
                sdk_version=credentials.sdk_version,
                address=credentials.address,
                scan_timeout=OPTIONS.scan_timeout,
                command_timeout=OPTIONS.command_timeout,
                transfer_timeout=OPTIONS.transfer_timeout,
                device_secret=credentials.device_secret,
            )
        )
    if not ogg.exists():
        convert_dtyj(dtyj, ogg, serial=fid & 0xFFFFFFFF)


class MemoCaptureService:
    """Own the A1 BLE connection while waiting for short voice-memo pushes."""

    def __init__(self) -> None:
        self._guard = threading.RLock()
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._desired = False
        self._status = {
            "enabled": False,
            "state": "stopped",
            "stream_enabled": False,
            "current_fid": None,
            "packets_received": 0,
            "current_duration_seconds": 0,
            "last_event": None,
            "last_saved_fid": None,
            "error": None,
        }

    def snapshot(self) -> dict:
        with self._guard:
            return dict(self._status)

    def _set(self, **values) -> None:
        with self._guard:
            self._status.update(values)

    def start(self) -> None:
        with self._guard:
            self._desired = True
            self._status.update(enabled=True, error=None, last_event="正在启动监听")
            if self._thread and self._thread.is_alive():
                return
            self._stop = threading.Event()
            self._thread = threading.Thread(target=self._worker, name="a1-memo-listener", daemon=True)
            self._thread.start()

    def stop(self) -> None:
        with self._guard:
            self._desired = False
            self._status["enabled"] = False
            thread = self._thread
            self._stop.set()
        if thread and thread is not threading.current_thread():
            thread.join(timeout=OPTIONS.scan_timeout + OPTIONS.command_timeout + 3)
        if not thread or not thread.is_alive():
            self._set(
                state="stopped",
                stream_enabled=False,
                current_fid=None,
                packets_received=0,
                current_duration_seconds=0,
                last_event="监听已停止",
            )

    def pause_for_operation(self) -> bool:
        with self._guard:
            restart = self._desired
        if restart:
            self.stop()
        return restart

    def resume_after_operation(self, restart: bool) -> None:
        if restart:
            self.start()

    def _worker(self) -> None:
        delay = 1.0
        while True:
            with self._guard:
                if not self._desired:
                    break
            acquired = DEVICE_OPERATION_LOCK.acquire(timeout=1)
            if not acquired:
                continue
            try:
                asyncio.run(self._listen_once())
                delay = 1.0
            except Exception as error:
                self._set(
                    state="error",
                    stream_enabled=False,
                    error=str(error),
                    current_fid=None,
                    last_event="连接或推流失败",
                )
            finally:
                DEVICE_OPERATION_LOCK.release()
            with self._guard:
                continuing = self._desired
            if not continuing:
                break
            self._stop.wait(delay)
            delay = min(delay * 2, 10.0)
        self._set(state="stopped", stream_enabled=False, current_fid=None)

    async def _listen_once(self) -> None:
        global LAST_STATE
        credentials = configured_args()
        self._set(
            state="connecting",
            stream_enabled=False,
            error=None,
            current_fid=None,
            packets_received=0,
            current_duration_seconds=0,
            last_event="正在扫描并鉴权 A1",
        )
        device = await find_a1(OPTIONS.scan_timeout, credentials.address)
        receiver = FrameReceiver()
        memo_args = SimpleNamespace(
            **vars(credentials),
            command_timeout=OPTIONS.command_timeout,
            output_dir=Path(OPTIONS.output_dir),
            api_key_file=Path(OPTIONS.api_key_file),
            api_key=configured_api_key(),
            asr_model=OPTIONS.asr_model,
            transcribe=True,
        )
        current: Capture | None = None
        async with BleakClient(device, timeout=OPTIONS.command_timeout) as client:
            await authenticate_memo(client, receiver, memo_args)
            stream_was_enabled = False
            try:
                await client.write_gatt_char(
                    COMMAND_CHAR,
                    make_frame(0x0100, 0x62, stream_control_body(credentials.did, True)),
                    response=True,
                )
                enable_result = await receiver.wait_json(0x0100, OPTIONS.command_timeout)
                if enable_result.get("code") != 200:
                    raise RuntimeError(f"live-stream enable rejected: {enable_result}")
                stream_was_enabled = True
                self._set(
                    state="waiting",
                    stream_enabled=True,
                    error=None,
                    last_event="蓝牙已连接，实时推流已开启",
                )
                while client.is_connected and not self._stop.is_set():
                    try:
                        command, _sequence, payload = await asyncio.wait_for(receiver.queue.get(), 0.75)
                    except asyncio.TimeoutError:
                        continue
                    if command == 0x0100:
                        body = parse_memo_json(payload)
                        if not body:
                            continue
                        if body.get("action") == "start":
                            if current and current.packets:
                                await save_capture(current, Path(OPTIONS.output_dir), memo_args)
                                self._set(last_saved_fid=current.fid)
                            current = Capture(fid=int(body.get("fid") or int(time.time())))
                            self._set(
                                state="recording",
                                current_fid=current.fid,
                                packets_received=0,
                                current_duration_seconds=0,
                                last_event="收到 A1 开始录音事件",
                            )
                        elif body.get("action") == "stop" and current:
                            completed, current = current, None
                            if completed.packets:
                                self._set(
                                    state="transcribing" if configured_api_key() else "saving",
                                    last_event="录音结束，正在保存",
                                )
                                await save_capture(completed, Path(OPTIONS.output_dir), memo_args)
                                self._set(last_saved_fid=completed.fid)
                                with STATE_LOCK:
                                    LAST_STATE = merge_local_files(LAST_STATE)
                            self._set(
                                state="waiting",
                                current_fid=None,
                                packets_received=0,
                                current_duration_seconds=0,
                                last_event=f"语音备忘录 {completed.fid} 已保存",
                            )
                    elif command == 0x0116:
                        body = parse_memo_json(payload)
                        if body and current:
                            fid = int(body.get("fid") or current.fid)
                            if fid == current.fid:
                                current.attributes.update(body)
                                self._set(last_event="已收到音频格式信息")
                    elif command == 0x0117:
                        try:
                            fid, block_sequence, packets = parse_audio_push(payload)
                        except ValueError:
                            continue
                        if not packets:
                            continue
                        if current is None:
                            current = Capture(fid=fid)
                            self._set(state="recording", current_fid=fid)
                        if fid != current.fid:
                            continue
                        signature = (block_sequence, b"".join(packets))
                        if signature not in current.seen_blocks:
                            current.seen_blocks.add(signature)
                            current.blocks += 1
                            current.packets.extend(packets)
                            self._set(
                                state="recording",
                                packets_received=len(current.packets),
                                current_duration_seconds=round(len(current.packets) * 0.02, 2),
                                last_event=f"正在接收音频块 {current.blocks}",
                            )
            finally:
                if current and current.packets:
                    await save_capture(current, Path(OPTIONS.output_dir), memo_args)
                    self._set(last_saved_fid=current.fid)
                    with STATE_LOCK:
                        LAST_STATE = merge_local_files(LAST_STATE)
                if stream_was_enabled and client.is_connected:
                    try:
                        await client.write_gatt_char(
                            COMMAND_CHAR,
                            make_frame(0x0100, 0x63, stream_control_body(credentials.did, False)),
                            response=True,
                        )
                        disable_result = await receiver.wait_json(0x0100, OPTIONS.command_timeout)
                        if disable_result.get("code") != 200:
                            self._set(error=f"实时推流关闭响应异常：{disable_result}")
                    except Exception as error:
                        self._set(error=f"实时推流关闭失败：{error}")
                self._set(stream_enabled=False)


MEMO_SERVICE = MemoCaptureService()


def public_state() -> dict:
    with STATE_LOCK:
        state = merge_local_files(LAST_STATE)
    state["memo"] = MEMO_SERVICE.snapshot()
    state["ai_key_configured"] = bool(configured_api_key())
    return state


class ConsoleHandler(SimpleHTTPRequestHandler):
    server_version = "A1Console/0.3"

    def log_request(self, code: int | str = "-", size: int | str = "-") -> None:
        """Log the path without ever writing the access-token query string."""
        self.log_message('"%s %s" %s %s', self.command, urlparse(self.path).path, code, size)

    def is_authorized(self) -> bool:
        expected = OPTIONS.access_token
        if not expected:
            return True
        query = parse_qs(urlparse(self.path).query)
        provided = self.headers.get("X-A1-Access-Token", "") or query.get("token", [""])[0]
        return access_token_matches(expected, provided)

    def require_authorization(self) -> bool:
        if self.is_authorized():
            return True
        self.send_json(
            {"error": "访问令牌无效；请从电脑启动窗口复制完整链接"},
            status=HTTPStatus.UNAUTHORIZED,
        )
        return False

    def send_json(self, value: dict, status: int = 200) -> None:
        data = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def read_json(self, maximum: int = 2_000_000) -> dict:
        length = int(self.headers.get("Content-Length", "0"))
        if length < 0 or length > maximum:
            raise ValueError("request body is too large")
        value = json.loads(self.rfile.read(length) or b"{}")
        if not isinstance(value, dict):
            raise ValueError("request body must be a JSON object")
        return value

    def do_GET(self) -> None:
        path = unquote(urlparse(self.path).path)
        if path == "/api/state":
            if not self.require_authorization():
                return
            self.send_json(public_state())
            return
        if path.startswith("/recordings/"):
            if not self.require_authorization():
                return
            filename = Path(path.removeprefix("/recordings/")).name
            target = Path(OPTIONS.output_dir) / filename
            if target.suffix.lower() != ".ogg" or not target.exists():
                self.send_error(HTTPStatus.NOT_FOUND)
                return
            total_size = target.stat().st_size
            try:
                byte_range = parse_byte_range(self.headers.get("Range"), total_size)
            except (TypeError, ValueError):
                self.send_response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
                self.send_header("Content-Range", f"bytes */{total_size}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            start, end = byte_range or (0, total_size - 1)
            content_length = end - start + 1
            self.send_response(HTTPStatus.PARTIAL_CONTENT if byte_range else HTTPStatus.OK)
            self.send_header("Content-Type", mimetypes.guess_type(target.name)[0] or "audio/ogg")
            self.send_header("Accept-Ranges", "bytes")
            if byte_range:
                self.send_header("Content-Range", f"bytes {start}-{end}/{total_size}")
            self.send_header("Content-Length", str(content_length))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            with target.open("rb") as recording:
                recording.seek(start)
                self.wfile.write(recording.read(content_length))
            return
        if not DIST_DIR.exists():
            self.send_error(HTTPStatus.SERVICE_UNAVAILABLE, "build console first: pnpm build")
            return
        requested = (DIST_DIR / (path.lstrip("/") or "index.html")).resolve()
        if DIST_DIR.resolve() not in requested.parents or not requested.is_file():
            requested = DIST_DIR / "index.html"
        content = requested.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", mimetypes.guess_type(requested.name)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(content)

    def do_POST(self) -> None:
        global API_KEY_OVERRIDE, LAST_STATE
        path = urlparse(self.path).path
        if not self.require_authorization():
            return
        try:
            if path == "/api/refresh":
                restart_memo = MEMO_SERVICE.pause_for_operation()
                if not acquire_device_operation_lock():
                    MEMO_SERVICE.resume_after_operation(restart_memo)
                    self.send_json({"error": "另一项 A1 操作正在进行"}, status=HTTPStatus.CONFLICT)
                    return
                try:
                    LAST_STATE = asyncio.run(inspect_a1())
                finally:
                    DEVICE_OPERATION_LOCK.release()
                    MEMO_SERVICE.resume_after_operation(restart_memo)
                self.send_json(public_state())
                return
            if path == "/api/download":
                body = self.read_json()
                fid = int(body["fid"])
                known = indexed_device_fids(LAST_STATE)
                if fid not in known:
                    raise ValueError("fid is not present in the current device index")
                restart_memo = MEMO_SERVICE.pause_for_operation()
                if not acquire_device_operation_lock():
                    MEMO_SERVICE.resume_after_operation(restart_memo)
                    self.send_json({"error": "另一项 A1 操作正在进行"}, status=HTTPStatus.CONFLICT)
                    return
                try:
                    asyncio.run(download_recording(fid))
                finally:
                    DEVICE_OPERATION_LOCK.release()
                    MEMO_SERVICE.resume_after_operation(restart_memo)
                LAST_STATE = merge_local_files(LAST_STATE)
                self.send_json({"ok": True, "state": public_state()})
                return
            if path == "/api/settings/api-key":
                body = self.read_json(maximum=20_000)
                key = str(body.get("api_key", "")).strip()
                if key and len(key) < 20:
                    raise ValueError("API Key 格式不正确")
                API_KEY_OVERRIDE = key
                self.send_json({"ok": True, "ai_key_configured": bool(configured_api_key())})
                return
            if path == "/api/memo/start":
                MEMO_SERVICE.start()
                self.send_json({"ok": True, "memo": MEMO_SERVICE.snapshot()})
                return
            if path == "/api/memo/stop":
                MEMO_SERVICE.stop()
                self.send_json({"ok": True, "memo": MEMO_SERVICE.snapshot()})
                return
            if path == "/api/delete-local":
                body = self.read_json()
                if body.get("confirmed") is not True:
                    raise ValueError("explicit deletion confirmation is required")
                fid = int(body["fid"])
                kind = str(body.get("kind") or "recording")
                removed = delete_local_recording(fid, kind)
                LAST_STATE = merge_local_files(
                    {
                        **LAST_STATE,
                        "recordings": [
                            item for item in LAST_STATE.get("recordings", [])
                            if not (
                                int(item["fid"]) == fid
                                and item.get("on_device", True) is False
                            )
                        ],
                    }
                )
                self.send_json(
                    {
                        "ok": True,
                        "deleted_fid": fid,
                        "removed_files": removed,
                        "state": public_state(),
                    }
                )
                return
            if path == "/api/transcribe":
                body = self.read_json()
                fid = int(body["fid"])
                api_key = configured_api_key()
                if not api_key:
                    raise ValueError("请先在设置中填写硅基流动 API Key")
                audio_path, _metadata_path = metadata_paths(fid)
                if not audio_path.exists():
                    if fid not in indexed_device_fids(LAST_STATE):
                        raise ValueError("本机和当前设备列表中都找不到这条录音")
                    restart_memo = MEMO_SERVICE.pause_for_operation()
                    if not acquire_device_operation_lock():
                        MEMO_SERVICE.resume_after_operation(restart_memo)
                        self.send_json({"error": "另一项 A1 操作正在进行"}, status=HTTPStatus.CONFLICT)
                        return
                    try:
                        asyncio.run(download_recording(fid))
                    finally:
                        DEVICE_OPERATION_LOCK.release()
                        MEMO_SERVICE.resume_after_operation(restart_memo)
                    audio_path, _metadata_path = metadata_paths(fid)
                try:
                    transcription = transcribe_sync(audio_path, api_key, OPTIONS.asr_model)
                    update_metadata(
                        fid,
                        transcription=transcription,
                        transcription_error=None,
                        transcription_model=OPTIONS.asr_model,
                        transcribed_at=datetime.now().astimezone().isoformat(),
                    )
                except Exception as error:
                    update_metadata(fid, transcription_error=str(error))
                    raise
                LAST_STATE = merge_local_files(LAST_STATE)
                self.send_json({"ok": True, "transcription": transcription, "state": public_state()})
                return
            if path == "/api/transcript":
                body = self.read_json()
                fid = int(body["fid"])
                transcription = str(body.get("transcription", "")).strip()
                audio_path, _metadata_path = metadata_paths(fid)
                if not audio_path.exists():
                    raise ValueError("请先下载这条录音")
                update_metadata(fid, transcription=transcription, transcription_error=None)
                LAST_STATE = merge_local_files(LAST_STATE)
                self.send_json({"ok": True, "state": public_state()})
                return
            if path == "/api/summarize":
                body = self.read_json()
                fid = int(body["fid"])
                api_key = configured_api_key()
                if not api_key:
                    raise ValueError("请先在设置中填写硅基流动 API Key")
                _audio_path, metadata_path = metadata_paths(fid)
                metadata = load_metadata(metadata_path)
                transcription = str(metadata.get("transcription") or "").strip()
                if not transcription:
                    raise ValueError("请先完成转写")
                try:
                    summary = summarize_sync(transcription, api_key, OPTIONS.summary_model)
                    update_metadata(
                        fid,
                        summary=summary,
                        summary_error=None,
                        summary_model=OPTIONS.summary_model,
                        summarized_at=datetime.now().astimezone().isoformat(),
                    )
                except Exception as error:
                    update_metadata(fid, summary_error=str(error))
                    raise
                LAST_STATE = merge_local_files(LAST_STATE)
                self.send_json({"ok": True, "summary": summary, "state": public_state()})
                return
            if path == "/api/delete":
                body = self.read_json()
                fid = int(body["fid"])
                known = indexed_device_fids(LAST_STATE)
                local_memo = Path(OPTIONS.output_dir) / f"memo-{fid}.ogg"
                if local_memo.exists():
                    if body.get("confirmed") is not True:
                        raise ValueError("explicit deletion confirmation is required")
                    local_metadata = local_memo.with_suffix(".json")
                    local_memo.unlink()
                    if local_metadata.exists():
                        local_metadata.unlink()
                    LAST_STATE = merge_local_files(LAST_STATE)
                    self.send_json({"ok": True, "deleted_fid": fid, "local_only": True, "state": public_state()})
                    return
                if fid not in known:
                    raise ValueError("fid is not present in the current device index")
                if body.get("confirmed") is not True:
                    raise ValueError("explicit deletion confirmation is required")
                restart_memo = MEMO_SERVICE.pause_for_operation()
                if not acquire_device_operation_lock():
                    MEMO_SERVICE.resume_after_operation(restart_memo)
                    self.send_json({"error": "另一项 A1 操作正在进行"}, status=HTTPStatus.CONFLICT)
                    return
                try:
                    result = asyncio.run(delete_recording(fid))
                finally:
                    DEVICE_OPERATION_LOCK.release()
                    MEMO_SERVICE.resume_after_operation(restart_memo)
                LAST_STATE = merge_local_files(
                    {
                        **LAST_STATE,
                        "recordings": [
                            item for item in LAST_STATE.get("recordings", [])
                            if int(item["fid"]) != fid
                        ],
                    }
                )
                self.send_json({"ok": True, "deleted_fid": fid, "device_response": result, "state": public_state()})
                return
            self.send_error(HTTPStatus.NOT_FOUND)
        except Exception as error:
            self.send_json({"error": str(error)}, status=HTTPStatus.INTERNAL_SERVER_ERROR)


def main() -> int:
    global OPTIONS
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", default=str(ROOT_DIR / ".a1-device.json"))
    parser.add_argument("--output-dir", default=str(ROOT_DIR / "recordings"))
    parser.add_argument("--address")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--access-token", default=os.environ.get("A1_CONSOLE_TOKEN", ""))
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--command-timeout", type=float, default=15.0)
    parser.add_argument("--transfer-timeout", type=float, default=90.0)
    parser.add_argument("--api-key-file", default=str(ROOT_DIR / ".siliconflow-api-key"))
    parser.add_argument("--asr-model", default="FunAudioLLM/SenseVoiceSmall")
    parser.add_argument("--summary-model", default="Qwen/Qwen3-8B")
    parser.add_argument("--memo-autostart", action="store_true")
    parser.add_argument("--open", action="store_true", dest="open_browser")
    OPTIONS = parser.parse_args()
    if OPTIONS.host not in ("127.0.0.1", "localhost", "::1", "0.0.0.0"):
        raise RuntimeError("host must be loopback or 0.0.0.0")
    if OPTIONS.host == "0.0.0.0" and len(OPTIONS.access_token) < 24:
        raise RuntimeError("LAN mode requires an access token containing at least 24 characters")
    configured_args()
    server = ThreadingHTTPServer((OPTIONS.host, OPTIONS.port), ConsoleHandler)
    query = f"?token={quote(OPTIONS.access_token)}" if OPTIONS.access_token else ""
    local_url = f"http://127.0.0.1:{OPTIONS.port}/{query}"
    print(f"A1 local console: {local_url}")
    if OPTIONS.host == "0.0.0.0":
        for address in discover_lan_ipv4_addresses():
            print(f"A1 phone console: http://{address}:{OPTIONS.port}/{query}")
    if OPTIONS.open_browser:
        webbrowser.open(local_url)
    if OPTIONS.memo_autostart:
        MEMO_SERVICE.start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        MEMO_SERVICE.stop()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
