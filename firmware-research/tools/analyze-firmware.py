"""Static inspection only; never execute firmware or its OTA script."""
import base64
import hashlib
import json
import re
import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VERSION = "V1.6.88-202601291628"
ZIP = ROOT / f"DingTalk-A1-{VERSION}.zip"
UNPACKED = ROOT / "unpacked-V1.6.88"
OUT = ROOT / "analysis"

CATEGORIES = {
    "usb": r"usb|usbd|usbc|adb|hidraw|hiddev|hid_|hid |cdcacm|mass.storage|Composite device|rndis|mtp",
    "buttons-haptic": r"vibrat|haptic|motor|key_event|keycode|button|key_short|key_long|voice_memo",
    "ota": r"ota[._ /]|/ota|firmware|partition|signature|secure.?boot|CERT.RSA|md5sum",
    "filesystem": r"/emmc|/data|/etc|/dev/|\.opus|\.ogg|\.wav|\.pcm|record.*path|file_list",
    "console": r"nsh|telnet|shell|uart|jtag|swd|gdb|console|debugport",
    "protocol": r"stream_type|verify_code|ota@|deviceSecret|vibra|0x0100|wifi.*(ap|port)|ssid|socket|bind|token",
}


def main():
    OUT.mkdir(exist_ok=True)
    with zipfile.ZipFile(ZIP) as z:
        bad = z.testzip()
        if bad:
            raise ValueError(f"ZIP CRC failure: {bad}")
    # Validate the checksums supplied inside the vendor package.
    md5_checks = []
    for line in (UNPACKED / "md5sum").read_text().splitlines():
        name, expected = line.split(maxsplit=1)
        name = name.lstrip("*").removeprefix("./")
        actual = hashlib.md5((UNPACKED / name).read_bytes()).hexdigest()
        md5_checks.append({"file": name, "matches_vendor_md5": actual == expected})
    sha_checks = []
    manifest = (UNPACKED / "META-INF" / "MANIFEST.MF").read_text()
    for name, expected in re.findall(r"Name: ([^\n]+)\nSHA1-Digest: ([^\n]+)", manifest):
        actual = base64.b64encode(hashlib.sha1((UNPACKED / name).read_bytes()).digest()).decode()
        sha_checks.append({"file": name, "matches_vendor_manifest": actual == expected})
    images = []
    category_lines = {name: [] for name in CATEGORIES}
    for path in sorted(UNPACKED.glob("*.bin")):
        data = path.read_bytes()
        strings = [(m.start(), m.group().decode("ascii")) for m in re.finditer(rb"[\x20-\x7e]{5,}", data)]
        (OUT / (path.name + ".strings.txt")).write_text(
            "\n".join(f"0x{offset:08x}\t{text}" for offset, text in strings), encoding="utf-8")
        for category, pattern in CATEGORIES.items():
            for offset, text in strings:
                if re.search(pattern, text, re.I):
                    category_lines[category].append(f"{path.name}:0x{offset:08x}\t{text}")
        images.append({"file": path.name, "size": len(data), "header_hex": data[:64].hex(),
                       "first_u32_le": [f"0x{x:08x}" for x in struct.unpack("<16I", data[:64])],
                       "ascii_strings": len(strings)})
    for category, lines in category_lines.items():
        (OUT / (category + ".txt")).write_text("\n".join(lines), encoding="utf-8")
    result = {"version": VERSION, "download_size": ZIP.stat().st_size, "zip_crc_ok": True,
              "vendor_md5": md5_checks, "vendor_manifest": sha_checks,
              "images": images, "category_counts": {k: len(v) for k, v in category_lines.items()}}
    (OUT / "inventory.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
