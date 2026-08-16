"""Read-only BLE scan for a DingTalk/TALIX A1 recorder."""

import argparse
import asyncio

from bleak import BleakScanner


A1_SERVICE = "0000fe3c-0000-1000-8000-00805f9b34fb"


async def scan(timeout: float) -> int:
    discovered = await BleakScanner.discover(timeout=timeout, return_adv=True)
    rows = []
    for device, advertisement in discovered.values():
        uuids = {value.lower() for value in advertisement.service_uuids}
        is_a1 = A1_SERVICE in uuids or "a1" in (device.name or "").lower()
        rows.append(
            (
                not is_a1,
                -(advertisement.rssi or -999),
                is_a1,
                device.name or advertisement.local_name or "<unnamed>",
                device.address,
                advertisement.rssi,
                sorted(uuids),
            )
        )

    rows.sort()
    print(f"Discovered {len(rows)} BLE devices in {timeout:.1f}s")
    for _, _, is_a1, name, address, rssi, uuids in rows:
        marker = "A1?" if is_a1 else "   "
        print(f"[{marker}] {name!r}  {address}  RSSI={rssi}  services={','.join(uuids) or '-'}")
    return 0 if any(row[2] for row in rows) else 2


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()
    return asyncio.run(scan(args.timeout))


if __name__ == "__main__":
    raise SystemExit(main())
