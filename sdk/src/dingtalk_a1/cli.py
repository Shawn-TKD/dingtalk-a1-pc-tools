"""a1 CLI: JSON output for apps and agents; diagnostic errors go to stderr."""
import argparse
import asyncio
import json
import os
from pathlib import Path
import sys

from .identity import Identity
from .ble import A1Client
from .capture import LiveRecorder
from .audio import convert_dtyj, export_memos
from .agent import ModeRouter
from .capabilities import CAPABILITIES


def emit(value, *, error=False):
    print(json.dumps(value, ensure_ascii=False, default=str), file=sys.stderr if error else sys.stdout, flush=True)


async def listen(args, identity):
    delay = 1
    while True:
        client = A1Client(identity)
        recorder = LiveRecorder(args.output_dir, device_id=identity.serial_number or identity.did)
        try:
            await client.connect(address=args.address)
            # No automatic recording start, setting changes or telemetry acknowledgements.
            async with client.subscribe() as queue:
                emit({"event": "ready", "message": "A1 connected; use its recording/voice key"})
                delay = 1
                while True:
                    frame = await client.next_frame(queue)
                    for event in recorder.feed(frame):
                        emit(event)
        except asyncio.CancelledError:
            raise
        except Exception as error:
            emit({"event": "disconnected", "error": type(error).__name__, "message": str(error)}, error=True)
            if not args.reconnect or "code=" in str(error) or isinstance(error, (ValueError, OSError)):
                raise
        finally:
            for event in recorder.close("connection_or_listener_ended"):
                emit(event)
            await client.close()
        await asyncio.sleep(delay)
        delay = min(delay * 2, 15)


async def run_ble(args):
    if args.command == "scan":
        emit([{"address": d.address, "name": d.name, "rssi": adv.rssi}
              for d, adv in await A1Client.scan(args.timeout)])
        return
    identity = Identity.load(args.config)
    if args.command == "listen":
        await listen(args, identity)
        return
    client = A1Client(identity)
    try:
        await client.connect(address=args.address)
        if args.command == "status":
            emit(await client.status())
        elif args.command == "files":
            emit(await client.recordings(limit=args.limit, since=args.since, until=args.until))
        elif args.command == "download":
            def progress(received, total):
                emit({"event": "progress", "received_bytes": received, "total_bytes": total}, error=True)
            result = await client.download(args.fid, args.output, on_progress=progress)
            if args.ogg:
                result["ogg"] = convert_dtyj(args.output, args.ogg, validate=args.validate)
            emit(result)
        elif args.command == "delete":
            emit(await client.delete_recording(args.fid))
        elif args.command == "record":
            emit(await client.recording_control(args.action))
        elif args.command == "settings":
            emit(await client.audio_settings())
        elif args.command == "live-upload":
            emit(await client.set_live_upload(args.state == "on"))
    finally:
        await client.close()


def run_usb(args):
    from .usb import A1USB, USBStorage
    identity = Identity.load(args.config)
    if args.operation in ("info", "auth", "enter-adb"):
        with A1USB(identity) as usb:
            method = {"info": usb.info, "auth": usb.authenticate, "enter-adb": usb.enter_adb}[args.operation]
            emit(method())
        return
    storage = USBStorage(identity.serial_number, adb=args.adb)
    storage.start_server()
    if args.operation == "ls":
        emit(storage.list(args.path))
    elif args.operation == "space":
        result = storage.space()
        emit({**result, "total_GB": round(result["total_bytes"] / 1e9, 2),
              "free_GB": round(result["free_bytes"] / 1e9, 2)})
    elif args.operation == "pull":
        emit(storage.download(args.path, args.output))
    elif args.operation == "push":
        emit(storage.upload(args.source, args.path, verify=args.verify))
    elif args.operation == "mkdir":
        storage.mkdir(args.path)
        emit({"created": args.path})
    elif args.operation == "rm":
        storage.delete_file(args.path)
        emit({"deleted": args.path})
    elif args.operation == "motor-test":
        emit({"result": storage.motor_test(), "kind": "usb_fixed_sequence"})


def parser():
    p = argparse.ArgumentParser(description="Unofficial A1 SDK; no firmware changes. JSON output.")
    p.add_argument("--config", default=os.environ.get("A1_CONFIG", ".a1-device.json"))
    p.add_argument("--address", help="Override saved BLE address (rescan on a new OS)")
    sub = p.add_subparsers(dest="command", required=True)
    sub.add_parser("capabilities")
    sub.add_parser("scan").add_argument("--timeout", type=float, default=12)
    sub.add_parser("status")
    files = sub.add_parser("files")
    files.add_argument("--limit", type=int, default=100)
    files.add_argument("--since", type=int, default=0)
    files.add_argument("--until", type=int, default=0)
    down = sub.add_parser("download")
    down.add_argument("fid", type=int)
    down.add_argument("output")
    down.add_argument("--ogg")
    down.add_argument("--validate", action="store_true")
    delete = sub.add_parser("delete", help="Irreversibly delete ONE device recording")
    delete.add_argument("fid", type=int)
    delete.add_argument("--yes", action="store_true", help="Confirm deletion")
    sub.add_parser("record").add_argument("action", choices=("start", "stop", "pause", "resume"))
    sub.add_parser("settings")
    sub.add_parser("live-upload").add_argument("state", choices=("on", "off"))
    live = sub.add_parser("listen", help="Long connection -> local Ogg + JSONL events")
    live.add_argument("--output-dir", type=Path, default=Path("recordings/live"))
    live.add_argument("--reconnect", action="store_true")
    convert = sub.add_parser("convert")
    convert.add_argument("source")
    convert.add_argument("output")
    convert.add_argument("--validate", action="store_true")
    memos = sub.add_parser("export-memos")
    memos.add_argument("source")
    memos.add_argument("directory")
    memos.add_argument("--validate", action="store_true")
    route = sub.add_parser("route", help="Route already-transcribed text; DOES NOT execute it")
    route.add_argument("text")
    route.add_argument("--mode", choices=("development", "ideas", "research", "review", "game"), default="ideas")
    usb = sub.add_parser("usb")
    usb.add_argument("--adb", default="adb")
    ops = usb.add_subparsers(dest="operation", required=True)
    for name in ("info", "auth", "space"):
        ops.add_parser(name)
    adb = ops.add_parser("enter-adb", help="Interrupts recording and BLE; remains in ADB after exit")
    adb.add_argument("--allow-interrupt", action="store_true")
    ops.add_parser("ls").add_argument("path", nargs="?", default="/emmc")
    pull = ops.add_parser("pull")
    pull.add_argument("path")
    pull.add_argument("output")
    push = ops.add_parser("push")
    push.add_argument("source")
    push.add_argument("path")
    push.add_argument("--verify", action="store_true", help="Optional full readback comparison")
    ops.add_parser("mkdir").add_argument("path", nargs="?", default="/emmc/mindlink")
    rm = ops.add_parser("rm")
    rm.add_argument("path")
    rm.add_argument("--yes", action="store_true")
    ops.add_parser("motor-test").add_argument("--yes", action="store_true")
    return p


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    p = parser()
    args = p.parse_args()
    # Reject unconfirmed mutations before opening BLE/HID/ADB.
    if (args.command == "delete" or (args.command == "usb" and args.operation in ("rm", "motor-test"))) and not args.yes:
        p.error("Explicit confirmation required: --yes")
    if args.command == "usb" and args.operation == "enter-adb" and not args.allow_interrupt:
        p.error("This stops recording/BLE and leaves ADB enabled. Use --allow-interrupt only when intended.")
    try:
        if args.command == "capabilities":
            emit(CAPABILITIES)
        elif args.command == "convert":
            emit(convert_dtyj(args.source, args.output, validate=args.validate))
        elif args.command == "export-memos":
            result = export_memos(args.source, args.directory, validate=args.validate)
            emit(result)
            return 2 if any("error" in item for item in result) else 0
        elif args.command == "route":
            emit(ModeRouter(args.mode).route(args.text))
        elif args.command == "usb":
            run_usb(args)
        else:
            asyncio.run(run_ble(args))
        return 0
    except KeyboardInterrupt:
        return 130
    except Exception as error:
        emit({"error": type(error).__name__, "message": str(error)}, error=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
