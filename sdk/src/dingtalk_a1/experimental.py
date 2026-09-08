"""Named static-analysis interfaces. New SDK + firmware behavior not yet live-verified.

No OTA, identity reset, arbitrary system command or guessed BLE motor command.
"""
from pathlib import Path
import ipaddress
import time
from urllib.parse import quote
from urllib.request import Request, build_opener, ProxyHandler

from .protocol import DeviceError, raw_file_block
from .usb import emmc_path


class Experimental:
    def __init__(self, client):
        self.client = client

    async def schedules(self):
        return await self.client.request(0x011a, {"action": "get"})

    async def replace_schedules(self, intervals):
        """Replaces the ENTIRE schedule. [] explicitly clears existing entries."""
        if len(intervals) > 20:
            raise ValueError("A1 supports at most 20 intervals")
        now = int(time.time())
        for item in intervals:
            if not (isinstance(item["sid"], int) and
                    isinstance(item["start"], int) and
                    isinstance(item["end"], int) and
                    item["start"] != 0 and item["start"] < item["end"] and
                    now < item["end"]):
                raise ValueError("Each schedule requires integer sid, nonzero start < end, and a future end")
        return await self.client.request(0x011a, {"action": "set", "current": now, "params": intervals})

    async def send_marker(self, fid, seconds):
        return await self.client.request(0x0102, {"fid": str(int(fid)), "ts": str(int(seconds)), "type": 2})

    async def open_wifi(self):
        """May cancel BLE file sync and inhibit sleep; caller must close_wifi later.

        Returns private SSID/password. It does NOT join the host to that network.
        """
        async with self.client._transfer_lock:
            return await self.client.request(0x0120, {"type": 0})

    async def close_wifi(self):
        return await self.client.request(0x0121)

    async def download_raw_file(self, device_path, destination, *, offset=0, on_progress=None):
        """Download one known /emmc path through recovered BLE commands 0x014A..0x014C.

        This is a read-only, firmware-static interface. A nonzero offset resumes
        only an existing ``destination.part`` whose length exactly matches it.
        """
        path = emmc_path(device_path)
        if len(path.encode("utf-8")) > 63:
            raise ValueError("Path exceeds the recovered 63-byte raw-transfer limit")
        if not isinstance(offset, int) or offset < 0:
            raise ValueError("offset must be a nonnegative integer")

        target = Path(destination)
        target.parent.mkdir(parents=True, exist_ok=True)
        part = target.with_name(target.name + ".part")
        if target.exists():
            raise FileExistsError(target)
        if offset == 0 and part.exists():
            raise FileExistsError(part)
        if offset and (not part.exists() or part.stat().st_size != offset):
            raise ValueError("Resume offset must equal the existing .part length")

        received = offset
        previous = None
        expected_sequence = 1
        crc_failures = 0
        active = False
        async with self.client._transfer_lock, self.client.subscribe((0x014b,)) as queue:
            try:
                info = await self.client.request(0x014a, {"path": path, "offset": offset})
                size = info.get("size")
                returned_offset = info.get("offset")
                block_size = info.get("block_size")
                if not isinstance(size, int) or not offset < size:
                    raise DeviceError("Invalid raw-transfer size response")
                if returned_offset != offset or block_size != 8000:
                    raise DeviceError("Raw-transfer parameters disagree with the request")

                active = True
                with part.open("ab" if offset else "xb") as output:
                    while received < size:
                        frame = await self.client.next_frame(queue, 90)
                        try:
                            sequence, data = raw_file_block(frame.payload)
                        except ValueError as error:
                            if "CRC mismatch" not in str(error) or crc_failures >= 3:
                                raise
                            crc_failures += 1
                            await self.client._send(
                                0x014b, {"code": 0x0193},
                                sequence=frame.sequence, kind=0x31)
                            continue

                        crc_failures = 0
                        if previous is not None and sequence == previous[0]:
                            if data != previous[1]:
                                raise DeviceError("Retransmitted raw block changed")
                        else:
                            if sequence != expected_sequence:
                                raise DeviceError("Non-contiguous raw-file blocks")
                            if not data or received + len(data) > size:
                                raise DeviceError("Raw-file block exceeds declared size")
                            output.write(data)
                            received += len(data)
                            previous = (sequence, data)
                            expected_sequence = sequence + 1
                            if on_progress:
                                on_progress(received, size)
                        await self.client._send(
                            0x014b, {"code": 200},
                            sequence=frame.sequence, kind=0x31)

                active = False
                part.rename(target)
                return {
                    "path": str(target.resolve()),
                    "device_path": path,
                    "bytes": received,
                    "resumed_from": offset,
                    "experimental": True,
                }
            finally:
                if active and self.client.connected:
                    try:
                        await self.client._send(0x014c, {"did": self.client.identity.did})
                    except (DeviceError, OSError):
                        pass
                    finally:
                        await self.client.close()


def wifi_download(ip, device_path, destination):
    """After the user joins the A1 AP, download a known /emmc file via HTTP GET.

    Use the IP returned by open_wifi (usually 192.168.1.1); no listing or upload.
    The recovered HTTP URI limit is 63 bytes. Firmware return port=0 means use 80.
    """
    address = ipaddress.ip_address(ip)
    if address.version != 4 or not address.is_private:
        raise ValueError("Expected the device's private IPv4 AP address")
    path = emmc_path(device_path)
    uri = quote(path[len("/emmc"):], safe="/")
    if not uri or len(uri.encode("ascii")) > 63:
        raise ValueError("Path exceeds the recovered A1 HTTP URI limit")
    target = Path(destination)
    target.parent.mkdir(parents=True, exist_ok=True)
    part = target.with_name(target.name + ".part")
    if target.exists() or part.exists():
        raise FileExistsError(target)
    opener = build_opener(ProxyHandler({}))  # Never send local A1 traffic to a configured cloud proxy.
    received = 0
    with opener.open(Request(f"http://{address}{uri}", method="GET"), timeout=30) as response:
        if response.status != 200:
            raise ValueError(f"Unexpected HTTP status {response.status}")
        expected = response.headers.get("Content-Length")
        with part.open("xb") as output:
            while chunk := response.read(65536):
                output.write(chunk)
                received += len(chunk)
        if expected is not None and received != int(expected):
            raise ValueError("Incomplete HTTP download; .part retained")
    if target.exists():
        raise FileExistsError(target)
    part.rename(target)
    return {"path": str(target.resolve()), "bytes": received, "experimental": True}
