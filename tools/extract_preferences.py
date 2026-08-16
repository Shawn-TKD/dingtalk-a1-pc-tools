"""Extract one owned A1 profile from an exported DingTalk preferences XML.

The source XML and generated config are credentials. This script never prints
the full deviceSecret and refuses to overwrite an existing output by default.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def _device_dicts(value):
    if isinstance(value, dict):
        if value.get("deviceSecret") and (value.get("sn") or value.get("deviceId")):
            yield value
        for child in value.values():
            yield from _device_dicts(child)
    elif isinstance(value, list):
        for child in value:
            yield from _device_dicts(child)


def load_devices(path: Path) -> list[dict]:
    root = ET.parse(path).getroot()
    found: list[dict] = []
    seen: set[tuple[str, str]] = set()
    for node in root.iter():
        name = node.attrib.get("name", "")
        if "device_list_id" not in name or not node.text:
            continue
        try:
            value = json.loads(node.text)
        except json.JSONDecodeError:
            continue
        for device in _device_dicts(value):
            identity = (str(device.get("sn", "")), str(device.get("deviceId", "")))
            if identity not in seen:
                seen.add(identity)
                found.append(device)
    if not found:
        raise RuntimeError("no A1 device with deviceSecret found in device_list_id")
    return found


def mask(value: object, keep: int = 4) -> str:
    text = str(value or "")
    if not text:
        return "-"
    if len(text) <= keep * 2:
        return "*" * len(text)
    return f"{text[:keep]}…{text[-keep:]}"


def select_device(devices: list[dict], serial: str | None) -> dict:
    if serial:
        matches = [item for item in devices if str(item.get("sn", "")) == serial]
        if len(matches) != 1:
            raise RuntimeError(f"serial matched {len(matches)} devices, expected exactly one")
        return matches[0]
    if len(devices) != 1:
        raise RuntimeError("multiple devices found; choose one with --serial")
    return devices[0]


def make_config(device: dict, args) -> dict:
    if not args.did:
        raise RuntimeError("--did is required when writing a runnable config")
    return {
        "did": args.did,
        "corp_id": str(device.get("corpId") or args.corp_id or ""),
        "device_secret": str(device["deviceSecret"]),
        "serial_number": str(device.get("sn", "")),
        "device_id": device.get("deviceId"),
        "model": args.model,
        "sdk_version": args.sdk_version,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("preferences", type=Path)
    parser.add_argument("--list", action="store_true", dest="list_devices")
    parser.add_argument("--serial")
    parser.add_argument("--did")
    parser.add_argument("--corp-id", help="fallback only when the device entry has no corpId")
    parser.add_argument("--model", default="Windows_A1_Client")
    parser.add_argument("--sdk-version", default="V2.1.3")
    parser.add_argument("--write-config", type=Path)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    devices = load_devices(args.preferences)
    if args.list_devices or not args.write_config:
        for index, device in enumerate(devices, 1):
            print(
                f"[{index}] sn={mask(device.get('sn'))} "
                f"deviceId={mask(device.get('deviceId'))} "
                f"corpId={mask(device.get('corpId'))} secret=present"
            )
    if args.write_config:
        selected = select_device(devices, args.serial)
        config = make_config(selected, args)
        if not config["corp_id"]:
            raise RuntimeError("corpId missing from XML; provide --corp-id")
        mode = "w" if args.force else "x"
        args.write_config.parent.mkdir(parents=True, exist_ok=True)
        with args.write_config.open(mode, encoding="utf-8", newline="\n") as stream:
            json.dump(config, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
        print(f"wrote local config: {args.write_config} (secret not displayed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
