"""Connect to nearby DingTalk A1 advertisements and enumerate GATT only."""

import argparse
import asyncio

from bleak import BleakClient, BleakScanner


A1_SERVICE = "0000fe3c-0000-1000-8000-00805f9b34fb"


async def probe(timeout: float) -> int:
    found = await BleakScanner.discover(timeout=timeout, return_adv=True)
    candidates = []
    for device, advertisement in found.values():
        uuids = {item.lower() for item in advertisement.service_uuids}
        if A1_SERVICE in uuids:
            candidates.append((advertisement.rssi, device))

    candidates.sort(key=lambda row: row[0], reverse=True)
    if not candidates:
        print("No A1 advertisement found")
        return 2

    for rssi, device in candidates:
        print(f"Trying {device.name!r} {device.address} RSSI={rssi}")
        try:
            async with BleakClient(device, timeout=15.0) as client:
                print(f"Connected={client.is_connected} address={client.address}")
                for service in client.services:
                    print(f"SERVICE {service.uuid} {service.description}")
                    for characteristic in service.characteristics:
                        props = ",".join(characteristic.properties)
                        print(
                            f"  CHAR {characteristic.uuid} handle={characteristic.handle} "
                            f"props={props}"
                        )
                return 0
        except Exception as exc:
            print(f"Failed {device.address}: {type(exc).__name__}: {exc}")

    return 3


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--timeout", type=float, default=12.0)
    args = parser.parse_args()
    return asyncio.run(probe(args.timeout))


if __name__ == "__main__":
    raise SystemExit(main())
