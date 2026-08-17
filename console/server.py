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
from dtyj_to_ogg import convert as convert_dtyj  # noqa: E402


LAST_STATE: dict = {"connected": False, "device": None, "recordings": []}
OPTIONS = None
DEVICE_OPERATION_LOCK = threading.Lock()


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
    return {
        "local_url": f"/recordings/{ogg.name}" if ogg.exists() else None,
        "local_ogg_bytes": ogg.stat().st_size if ogg.exists() else None,
        "local_dtyj_bytes": dtyj.stat().st_size if dtyj.exists() else None,
    }


def backup_exists(fid: int) -> bool:
    info = local_file_info(fid)
    return bool(info["local_ogg_bytes"] and info["local_dtyj_bytes"])


def delete_request_body(did: str, fid: int) -> dict:
    """Match the official sendFileDelete(String did, String fid) JSON contract."""
    return {"did": did, "fid": str(fid)}


def access_token_matches(expected: str, provided: str) -> bool:
    return not expected or hmac.compare_digest(provided, expected)


def merge_local_files(state: dict) -> dict:
    merged = json.loads(json.dumps(state))
    known_fids = set()
    for recording in merged.get("recordings", []):
        fid = int(recording["fid"])
        known_fids.add(fid)
        recording.setdefault("on_device", True)
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

    def do_GET(self) -> None:
        path = unquote(urlparse(self.path).path)
        if path == "/api/state":
            if not self.require_authorization():
                return
            self.send_json(merge_local_files(LAST_STATE))
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
        global LAST_STATE
        path = urlparse(self.path).path
        if not self.require_authorization():
            return
        try:
            if path == "/api/refresh":
                if not DEVICE_OPERATION_LOCK.acquire(blocking=False):
                    self.send_json({"error": "另一项 A1 操作正在进行"}, status=HTTPStatus.CONFLICT)
                    return
                try:
                    LAST_STATE = asyncio.run(inspect_a1())
                finally:
                    DEVICE_OPERATION_LOCK.release()
                self.send_json(LAST_STATE)
                return
            if path == "/api/download":
                length = int(self.headers.get("Content-Length", "0"))
                body = json.loads(self.rfile.read(length) or b"{}")
                fid = int(body["fid"])
                known = indexed_device_fids(LAST_STATE)
                if fid not in known:
                    raise ValueError("fid is not present in the current device index")
                if not DEVICE_OPERATION_LOCK.acquire(blocking=False):
                    self.send_json({"error": "另一项 A1 操作正在进行"}, status=HTTPStatus.CONFLICT)
                    return
                try:
                    asyncio.run(download_recording(fid))
                finally:
                    DEVICE_OPERATION_LOCK.release()
                LAST_STATE = merge_local_files(LAST_STATE)
                self.send_json({"ok": True, "state": LAST_STATE})
                return
            if path == "/api/delete":
                length = int(self.headers.get("Content-Length", "0"))
                body = json.loads(self.rfile.read(length) or b"{}")
                fid = int(body["fid"])
                known = indexed_device_fids(LAST_STATE)
                if fid not in known:
                    raise ValueError("fid is not present in the current device index")
                if body.get("confirmed") is not True:
                    raise ValueError("explicit deletion confirmation is required")
                if not DEVICE_OPERATION_LOCK.acquire(blocking=False):
                    self.send_json({"error": "另一项 A1 操作正在进行"}, status=HTTPStatus.CONFLICT)
                    return
                try:
                    result = asyncio.run(delete_recording(fid))
                finally:
                    DEVICE_OPERATION_LOCK.release()
                LAST_STATE = merge_local_files(
                    {
                        **LAST_STATE,
                        "recordings": [
                            item for item in LAST_STATE.get("recordings", [])
                            if int(item["fid"]) != fid
                        ],
                    }
                )
                self.send_json({"ok": True, "deleted_fid": fid, "device_response": result, "state": LAST_STATE})
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
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
