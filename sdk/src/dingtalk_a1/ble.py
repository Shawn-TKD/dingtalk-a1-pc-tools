"""Persistent BLE connection; replies and unsolicited audio have separate routes."""
import asyncio
from contextlib import asynccontextmanager
from dataclasses import dataclass
from pathlib import Path
import time

from .identity import Identity
from .protocol import (SERVICE, WRITE, NOTIFY, Decoder, DeviceError, encode,
                       accepted, file_index, file_block, find_size)


@dataclass
class _Subscription:
    commands: set[int]
    queue: asyncio.Queue


class A1Client:
    def __init__(self, identity: Identity, *, timeout=20.0, scan_timeout=12.0):
        self.identity = identity
        self.timeout, self.scan_timeout = timeout, scan_timeout
        self._client = None
        self._decoder = Decoder()
        self._sequence = 15
        self._requests = {}
        self._subscribers = []
        self._command_lock = asyncio.Lock()
        self._write_lock = asyncio.Lock()
        self._transfer_lock = asyncio.Lock()
        self._closed = asyncio.Event()
        self._error = None

    @staticmethod
    async def scan(timeout=12.0):
        from bleak import BleakScanner
        found = await BleakScanner.discover(timeout=timeout, return_adv=True)
        return [(device, adv) for device, adv in found.values()
                if SERVICE in {s.lower() for s in adv.service_uuids}]

    @property
    def connected(self):
        return bool(self._client and self._client.is_connected and not self._closed.is_set())

    async def connect(self, *, address=None):
        if self.connected:
            return self
        if self._client is not None:
            await self.close()
        from bleak import BleakClient
        target = address or self.identity.address
        found = await self.scan(self.scan_timeout)
        if target:
            found = [(d, a) for d, a in found if d.address.casefold() == target.casefold()]
        if len(found) != 1:
            raise DeviceError(f"Found {len(found)} matching A1 devices; wake yours and select its address using 'a1 scan'")
        self._decoder = Decoder()
        self._closed.clear()
        self._error = None
        self._client = BleakClient(found[0][0], timeout=self.timeout,
                                   disconnected_callback=lambda _: self._on_disconnect())
        try:
            await self._client.connect()
            await self._client.start_notify(NOTIFY, self._on_data)
            challenge = await self.request(0x0008, {"corp_id": self.identity.corp_id})
            await self.request(0x0133, {
                "corp_id": self.identity.corp_id, "token": self.identity.token(challenge["random"]),
                "model": self.identity.model, "sdk_ver": self.identity.sdk_version,
                "timestamp": str(int(time.time())),
            })
        except BaseException:
            await self.close()
            raise
        return self

    async def close(self):
        client, self._client = self._client, None
        try:
            if client and client.is_connected:
                await client.disconnect()
        finally:
            self._on_disconnect()

    async def __aenter__(self):
        return await self.connect()

    async def __aexit__(self, *_):
        await self.close()

    def _on_disconnect(self, error=None):
        self._error = error or self._error or DeviceError("BLE disconnected")
        self._closed.set()
        for future in list(self._requests.values()):
            if not future.done():
                future.set_exception(self._error)

    def _on_data(self, _sender, data):
        try:
            for frame in self._decoder.feed(data):
                # Keep the owner-tested command correlation; serialize requests.
                # Device-originated requests do not satisfy a pending client request.
                future = self._requests.get(frame.command)
                if frame.kind == 0x31 and future is not None and not future.done():
                    future.set_result(frame)
                for sub in tuple(self._subscribers):
                    if frame.command in sub.commands:
                        sub.queue.put_nowait(frame)
        except (ValueError, asyncio.QueueFull) as error:
            # Never silently drop audio while reporting a complete recording.
            self._on_disconnect(DeviceError(f"Receive stream failed: {type(error).__name__}"))

    async def _send(self, command, body, *, sequence=None, kind=0x13):
        if not self.connected:
            raise self._error or DeviceError("Connect first")
        if sequence is None:
            self._sequence = (self._sequence + 1) & 255
            sequence = self._sequence
        packet = encode(command, sequence, body, kind)
        async with self._write_lock:
            size = min(180, max(20, self._client.mtu_size - 3))
            for offset in range(0, len(packet), size):
                await self._client.write_gatt_char(WRITE, packet[offset:offset + size], response=True)

    async def _request_frame(self, command, body=None):
        async with self._command_lock:
            future = asyncio.get_running_loop().create_future()
            self._requests[command] = future
            try:
                await self._send(command, {"did": self.identity.did, **(body or {})})
                return await asyncio.wait_for(future, self.timeout)
            except (TimeoutError, asyncio.CancelledError):
                # A late reply must not be mistaken for the next identical command.
                await self.close()
                raise
            finally:
                self._requests.pop(command, None)

    async def request(self, command, body=None):
        """Low-level research escape hatch. A code=200 alone is NOT proof of action.

        No command fuzzing: consult docs/PROTOCOL.md before using this method.
        """
        return accepted((await self._request_frame(command, body)).json(), (200, 202))

    @asynccontextmanager
    async def subscribe(self, commands=(0x0100, 0x0102, 0x0116, 0x0117, 0x000c, 0x0132)):
        """Subscribe BEFORE causing an event. The queue holds at most 256 frames."""
        sub = _Subscription(set(commands), asyncio.Queue(maxsize=256))
        self._subscribers.append(sub)
        try:
            yield sub.queue
        finally:
            self._subscribers.remove(sub)

    async def next_frame(self, queue, timeout=None):
        if self._closed.is_set():
            raise self._error or DeviceError("BLE disconnected")
        get = asyncio.create_task(queue.get())
        closed = asyncio.create_task(self._closed.wait())
        try:
            done, _ = await asyncio.wait((get, closed), timeout=timeout,
                                         return_when=asyncio.FIRST_COMPLETED)
            if self._closed.is_set():
                raise self._error or DeviceError("BLE disconnected")
            if get in done:
                return get.result()
            raise TimeoutError("Timed out waiting for a device event")
        finally:
            for task in (get, closed):
                task.cancel()
            await asyncio.gather(get, closed, return_exceptions=True)

    async def status(self):
        return await self.request(0x0132)

    async def recordings(self, *, since=0, until=0, limit=100):
        frame = await self._request_frame(0x0110, {
            "s_fid": str(since), "e_fid": str(until), "recently": limit})
        return file_index(frame.payload)

    async def recording_control(self, action):
        if action not in ("start", "stop", "pause", "resume"):
            raise ValueError("action must be start/stop/pause/resume")
        return await self.request(0x0100, {"action": action})

    async def audio_settings(self):
        return await self.request(0x0100, {"action": "get"})

    async def set_live_upload(self, enabled: bool):
        """Explicit setting change; connect()/listen do not change it automatically."""
        return await self.request(0x0100, {"action": "set", "params": [
            {"key": "upload_stream", "val": 1 if enabled else 2}]})

    async def delete_recording(self, fid: int):
        """Irreversibly delete ONE device recording. Does not delete local backups."""
        async with self._transfer_lock:
            return await self.request(0x0113, {"fid": str(int(fid))})

    async def download(self, fid: int, destination: str | Path, *, on_progress=None):
        """Save original DTYJ; no delete-after-sync. Streaming, no whole-file buffer."""
        target = Path(destination)
        target.parent.mkdir(parents=True, exist_ok=True)
        part = target.with_name(target.name + ".part")
        if target.exists() or part.exists():
            raise FileExistsError("Destination or unfinished .part already exists")
        received, expected, next_number, previous = 0, None, None, None
        active = False
        async with self._transfer_lock, self.subscribe((0x0114, 0x0115)) as queue:
            try:
                with part.open("xb") as output:
                    active = True
                    await self.request(0x0111, {"fid": str(int(fid)), "offset": 0, "progress": 65537})
                    attrs = await self.next_frame(queue, 90)
                    if attrs.command != 0x0114:
                        raise DeviceError("File block arrived before attributes")
                    expected = find_size(attrs.json())
                    await self._send(0x0114, {"code": 200}, sequence=attrs.sequence, kind=0x31)
                    while expected is None or received < expected:
                        frame = await self.next_frame(queue, 90)
                        if frame.command == 0x0114:  # Header retry after lost ACK.
                            await self._send(0x0114, {"code": 200}, sequence=frame.sequence, kind=0x31)
                            continue
                        block_fid, number, data = file_block(frame.payload)
                        if block_fid != int(fid):
                            raise DeviceError("Received a different recording's file block")
                        if previous and number == previous[0]:
                            if data != previous[1]:
                                raise DeviceError("Retransmitted block changed")
                        else:
                            if next_number is not None and number != next_number:
                                raise DeviceError("Non-contiguous file blocks; download not complete")
                            if not data:
                                raise DeviceError("Unexpected empty file block")
                            if expected is None:
                                if len(data) < 12 or data[:4] != b"BABA" or data[8:12] != b"DTYJ":
                                    raise DeviceError("Missing size and unsupported recording header")
                                expected = int.from_bytes(data[4:8], "little") + 8
                            if received + len(data) > expected:
                                raise DeviceError("File blocks exceed declared size")
                            output.write(data)
                            received += len(data)
                            next_number, previous = number + 1, (number, data)
                            if on_progress:
                                on_progress(received, expected)
                        await self._send(0x0115, {"code": 200}, sequence=frame.sequence, kind=0x31)
                active = False
                if target.exists():
                    raise FileExistsError(target)
                part.rename(target)
                return {"fid": int(fid), "path": str(target.resolve()), "bytes": received,
                        "attributes": attrs.json()}
            finally:
                if active and self.connected:
                    try:
                        await self._send(0x0112, {"did": self.identity.did, "fid": str(int(fid))})
                    except (DeviceError, OSError):
                        pass
                    finally:
                        # Do not let late blocks from a failed transfer enter the next one.
                        await self.close()
                # Failed .part is deliberately retained for inspection, never marked complete.

    @property
    def experimental(self):
        from .experimental import Experimental
        return Experimental(self)
