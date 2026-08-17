"""Loopback-only API and static server for the DingTalk A1 console."""

from __future__ import annotations

import argparse
import asyncio
from datetime import datetime
from http import HTTPStatus
from http.server import HTTPServer, SimpleHTTPRequestHandler
import json
import mimetypes
from pathlib import Path
import sys
from types import SimpleNamespace
from urllib.parse import unquote, urlparse
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


def merge_local_files(state: dict) -> dict:
    merged = json.loads(json.dumps(state))
    for recording in merged.get("recordings", []):
        recording.update(local_file_info(int(recording["fid"])))
    return merged


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
    server_version = "A1Console/0.2"

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
            self.send_json(merge_local_files(LAST_STATE))
            return
        if path.startswith("/recordings/"):
            filename = Path(path.removeprefix("/recordings/")).name
            target = Path(OPTIONS.output_dir) / filename
            if target.suffix.lower() != ".ogg" or not target.exists():
                self.send_error(HTTPStatus.NOT_FOUND)
                return
            content = target.read_bytes()
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", mimetypes.guess_type(target.name)[0] or "audio/ogg")
            self.send_header("Content-Length", str(len(content)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(content)
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
        self.end_headers()
        self.wfile.write(content)

    def do_POST(self) -> None:
        global LAST_STATE
        path = urlparse(self.path).path
        try:
            if path == "/api/refresh":
                LAST_STATE = asyncio.run(inspect_a1())
                self.send_json(LAST_STATE)
                return
            if path == "/api/download":
                length = int(self.headers.get("Content-Length", "0"))
                body = json.loads(self.rfile.read(length) or b"{}")
                fid = int(body["fid"])
                known = {int(item["fid"]) for item in LAST_STATE.get("recordings", [])}
                if fid not in known:
                    raise ValueError("fid is not present in the current device index")
                asyncio.run(download_recording(fid))
                LAST_STATE = merge_local_files(LAST_STATE)
                self.send_json({"ok": True, "state": LAST_STATE})
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
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--command-timeout", type=float, default=15.0)
    parser.add_argument("--transfer-timeout", type=float, default=90.0)
    parser.add_argument("--open", action="store_true", dest="open_browser")
    OPTIONS = parser.parse_args()
    if OPTIONS.host not in ("127.0.0.1", "localhost", "::1"):
        raise RuntimeError("refusing to expose a credentialed device console beyond loopback")
    configured_args()
    server = HTTPServer((OPTIONS.host, OPTIONS.port), ConsoleHandler)
    url = f"http://{OPTIONS.host}:{OPTIONS.port}"
    print(f"A1 local console: {url}")
    if OPTIONS.open_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
