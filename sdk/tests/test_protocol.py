import asyncio
from contextlib import contextmanager
import io
import json
import shlex
from pathlib import Path, PurePosixPath as PurePath
import struct
import tempfile
import time
import unittest

from dingtalk_a1 import A1Client, Identity, LiveRecorder, convert_dtyj
from dingtalk_a1.agent import ModeRouter
from dingtalk_a1.audio import make_page, ogg_crc, opus_samples
from dingtalk_a1.protocol import (Decoder, Frame, encode, file_index,
                                 file_index_page, file_block, raw_file_block, crc32_bzip2,
                                 DeviceError, HID_HEADER)
from dingtalk_a1.usb import emmc_path, USBStorage, recv_exact, A1USB


IDENTITY = Identity("test-did", "test-corp", "0123456789abcdef0123456789abcdef", "TEST-SN")


def frame(command, body, kind=0x13, seq=1):
    return Decoder().feed(encode(command, seq, body, kind))[0]


def make_dtyj():
    fmt = bytearray(48)
    fmt[:4] = b"fmt "
    struct.pack_into("<I", fmt, 12, 16000)
    struct.pack_into("<H", fmt, 24, 84)
    records = b"".join(struct.pack("<I", flag) + b"\x48" + b"\0" * 79 for flag in (0x20, 0x1a0, 1))
    data = b"data" + struct.pack("<I", len(records)) + b"\0" * 4 + records
    body = b"DTYJver v1.7" + fmt + data
    return b"BABA" + struct.pack("<I", len(body)) + body


def push(fid, number):
    payload = bytearray(28 + 84)
    payload[4:8] = fid.to_bytes(4, "big")
    struct.pack_into(">II", payload, 16, number, 84)
    payload[28:] = b"\x48" + b"\0" * 83
    return frame(0x0117, bytes(payload), kind=0x14)


class FormatTests(unittest.TestCase):
    def test_owner_auth_vector_and_secret_redaction(self):
        self.assertEqual(IDENTITY.token("0" * 32),
                         "2828efe22aacb2e1f9af33660ae2a746e8427749de950998174bf02aa7f77199")
        self.assertNotIn(IDENTITY.device_secret, repr(IDENTITY))

    def test_ble_usb_endianness_are_different(self):
        self.assertEqual(encode(0x0133, 17, {"a": 1})[:8].hex(), "1301331100000007")
        self.assertEqual(encode(400, 17, None, header=HID_HEADER).hex(), "1390011100000000")

    def test_fragmented_and_coalesced_notifications(self):
        decoder = Decoder()
        wire = encode(0x0132, 2, {"code": 200}, 0x31) + encode(0x0102, 3, {"fid": "1"})
        output = []
        for offset in range(0, len(wire), 3):
            output.extend(decoder.feed(wire[offset:offset + 3]))
        self.assertEqual([f.command for f in output], [0x0132, 0x0102])
        self.assertEqual(output[0].json()["code"], 200)

    def test_malformed_frame_length_cannot_grow_unbounded(self):
        decoder = Decoder()
        with self.assertRaises(ValueError):
            decoder.feed(bytes.fromhex("31013201ffffffff"))
        self.assertFalse(decoder.buffer)

    def test_file_index_raw_duration_preserved(self):
        result = file_index(bytes.fromhex("00c8000100056553f100ffff00005a5a"))
        self.assertEqual(result[0]["fid"], 1700000000)
        self.assertEqual(result[0]["status_or_duration_raw"], 65535)
        with self.assertRaises(ValueError):
            file_index(bytes.fromhex("00c80001"))

    def test_stock_file_index_truncation_trailer(self):
        payload = bytes.fromhex("00c8000100056553f100ffff000000015a5a5a5a")
        page = file_index_page(payload)
        self.assertTrue(page["tail_present"])
        self.assertTrue(page["truncated"])
        self.assertEqual(page["entries"][0]["fid"], 1700000000)

    def test_raw_file_block_crc(self):
        data = b"123456789"
        payload = struct.pack(">II", 7, len(data)) + data + struct.pack(">I", crc32_bzip2(data))
        self.assertEqual(crc32_bzip2(data), 0xfc891918)
        self.assertEqual(raw_file_block(payload), (7, data))
        with self.assertRaises(ValueError):
            raw_file_block(payload[:-1] + bytes([payload[-1] ^ 1]))

    def test_ordinary_file_block_trailer_crc(self):
        data = b"DTYJ"
        payload = (struct.pack(">HIHII", 0, 1700000000, 0, 4, len(data)) + data +
                   struct.pack(">II", 0, crc32_bzip2(data)))
        self.assertEqual(file_block(payload), (1700000000, 4, data))
        with self.assertRaises(ValueError):
            file_block(payload[:-1] + bytes([payload[-1] ^ 1]))

    def test_ogg_crc_and_variable_packet_duration(self):
        self.assertEqual(opus_samples(b"\x48\0"), 960)
        self.assertEqual(opus_samples(b"\x40\0"), 480)
        self.assertEqual(opus_samples(b"\x4b\x01\0"), 960)
        with self.assertRaises(ValueError):
            opus_samples(b"\x4b\x00")
        page = bytearray(make_page(b"\x48\0", 4, 960, 1, 2))
        declared = int.from_bytes(page[22:26], "little")
        page[22:26] = b"\0" * 4
        self.assertEqual(ogg_crc(page), declared)

    def test_dtyj_opaque_flags_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            source, target = Path(directory) / "a.dtyj", Path(directory) / "a.ogg"
            source.write_bytes(make_dtyj())
            result = convert_dtyj(source, target)
            self.assertEqual(result["packet_count"], 3)
            self.assertEqual(result["duration_seconds"], 0.06)
            self.assertFalse(result["full_decode_verified"])
            with self.assertRaises(FileExistsError):
                convert_dtyj(source, target)

    def test_live_duplicate_and_disconnect_are_not_complete(self):
        with tempfile.TemporaryDirectory() as directory:
            recorder = LiveRecorder(directory, device_id="TEST-SN")
            recorder.feed(frame(0x0116, {"fid": "1700000000", "stream_type": 0, "attrs": "audio@opus@16000@84@1"}))
            recorder.feed(push(1700000000, 7))
            recorder.feed(push(1700000000, 7))
            event = recorder.close("disconnected")[0]
            self.assertEqual(event["packet_count"], 1)
            self.assertFalse(event["complete"])
            self.assertEqual(event["stream_type"], 0)
            self.assertEqual(event["kind"], "live_audio")
            self.assertIn("incomplete", event["path"])

    def test_live_stop_and_gap_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            recorder = LiveRecorder(directory, device_id="TEST-SN")
            recorder.feed(push(1700000000, 0))
            recorder.feed(push(1700000000, 2))
            event = recorder.feed(frame(0x0100, {"action": "stop", "fid": "1700000000"}))[0]
            self.assertFalse(event["complete"])
            self.assertEqual(event["packet_count"], 2)
            recorder.feed(frame(0x0100, {"action": "start", "fid": "1700000001"}))
            recorder.feed(push(1700000001, 0))
            event = recorder.feed(frame(0x0100, {"action": "stop", "fid": "1700000001"}))[0]
            self.assertTrue(event["complete"])

    def test_mode_prefix_not_a_quoted_or_negated_word(self):
        router = ModeRouter()
        self.assertEqual(router.route("不要进入开发模式，记一条笔记")["mode"], "ideas")
        self.assertEqual(router.route("开发模式，修改按钮")["text"], "修改按钮")
        self.assertEqual(router.route("再改大一点")["mode"], "development")
        self.assertFalse(router.route("删除全部东西")["executed"])
        self.assertEqual(router.route("灵感模式")["event"], "mode_changed")

    def test_unknown_telemetry_does_not_destroy_audio_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            recorder = LiveRecorder(directory, device_id="TEST-SN")
            recorder.feed(frame(0x0100, {"action": "start", "fid": "1700000000"}))
            recorder.feed(push(1700000000, 0))
            event = recorder.feed(frame(0x000c, b"unknown"))[0]
            self.assertEqual(event["event"], "telemetry_unparsed")
            event = recorder.feed(frame(0x0100, {"body": {"action": "stop"}}))[0]
            self.assertTrue(event["complete"])

    def test_joining_mid_recording_cannot_report_complete(self):
        with tempfile.TemporaryDirectory() as directory:
            recorder = LiveRecorder(directory, device_id="TEST-SN")
            recorder.feed(push(1700000000, 42))
            event = recorder.feed(frame(0x0100, {"action": "stop"}))[0]
            self.assertFalse(event["complete"])
            self.assertIn("recording_start_not_observed", event["incomplete_reasons"])

    def test_usb_paths_prevent_shell_injection_and_recording_deletion(self):
        self.assertEqual(emmc_path("/emmc/mindlink/我的视频.mp4", write=True), "/emmc/mindlink/我的视频.mp4")
        for value in ("/dev/ap", "/emmc/mindlink/../audio/a", "/emmc/mindlink/$(reboot)",
                      "/emmc/mindlink/a\"", "/emmc/audio/recording", "/emmc/mindlink-old/a"):
            with self.assertRaises((ValueError, PermissionError)):
                emmc_path(value, write=True)


class FakeConnection:
    is_connected = True
    mtu_size = 23
    async def disconnect(self):
        self.is_connected = False


class SessionTests(unittest.IsolatedAsyncioTestCase):
    async def test_query_does_not_eat_simultaneous_marker(self):
        client = A1Client(IDENTITY)
        client._client = FakeConnection()
        async def send(cmd, body, **kwargs):
            client._on_data(None, encode(0x0102, 1, {"fid": "1700000000", "ts": "2", "type": 1}))
            client._on_data(None, encode(cmd, 2, {"code": 200}, 0x31))
        client._send = send
        async with client.subscribe((0x0102,)) as queue:
            self.assertEqual((await client.status())["code"], 200)
            self.assertEqual((await client.next_frame(queue, .1)).command, 0x0102)

    async def test_request_timeout_closes_connection_before_late_reply(self):
        client = A1Client(IDENTITY, timeout=.01)
        connection = client._client = FakeConnection()
        async def send(*args, **kwargs):
            pass
        client._send = send
        with self.assertRaises(TimeoutError):
            await client.status()
        self.assertFalse(connection.is_connected)
        self.assertFalse(client._requests)

    async def test_disconnect_wakes_pending_request_and_listener(self):
        client = A1Client(IDENTITY)
        client._client = FakeConnection()
        async def send(*args, **kwargs):
            client._on_disconnect()
        client._send = send
        async with client.subscribe() as queue:
            with self.assertRaises(DeviceError):
                await client.status()
            with self.assertRaises(DeviceError):
                await client.next_frame(queue)

    async def test_download_repeated_block_is_written_only_once(self):
        fid, data = 1700000000, make_dtyj()
        chunks = [(0, data[:100]), (0, data[:100]), (1, data[100:])]
        client = A1Client(IDENTITY)
        client._client = FakeConnection()
        def block(number, content):
            return struct.pack(">HIHII", 0, fid, 0, number, len(content)) + content
        async def send(cmd, body, **kwargs):
            if cmd == 0x0111:
                # Header can already be in flight when the accept response arrives.
                client._on_data(None, encode(0x0114, 3, {"size": len(data)}))
                client._on_data(None, encode(0x0111, 2, {"code": 200}, 0x31))
            elif cmd in (0x0114, 0x0115) and chunks:
                n, part = chunks.pop(0)
                client._on_data(None, encode(0x0115, 4, block(n, part)))
        client._send = send
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "test.dtyj"
            result = await client.download(fid, target)
            self.assertEqual(target.read_bytes(), data)
            self.assertEqual(result["bytes"], len(data))
            self.assertFalse(target.with_suffix(".dtyj.part").exists())

    async def test_queue_overflow_is_visible_instead_of_dropping_audio(self):
        client = A1Client(IDENTITY)
        client._client = FakeConnection()
        async with client.subscribe((0x0117,)) as queue:
            for _ in range(257):
                client._on_data(None, encode(0x0117, 1, b"audio", 0x14))
            with self.assertRaises(DeviceError):
                await client.next_frame(queue)

    async def test_schedule_accepts_an_ongoing_interval(self):
        client = A1Client(IDENTITY)
        captured = {}

        async def request(command, body=None):
            captured.update(command=command, body=body)
            return {"code": 200}

        client.request = request
        now = int(time.time())
        await client.experimental.replace_schedules([
            {"start": now - 10, "end": now + 60, "sid": 12}
        ])
        self.assertEqual(captured["command"], 0x011a)
        self.assertEqual(captured["body"]["params"][0]["sid"], 12)

    async def test_raw_ble_download_streams_verified_blocks(self):
        data = b"raw-device-file" * 700
        chunks = [data[:8000], data[8000:]]
        client = A1Client(IDENTITY)
        client._client = FakeConnection()

        def raw_block(sequence, content):
            return (struct.pack(">II", sequence, len(content)) + content +
                    struct.pack(">I", crc32_bzip2(content)))

        async def send(cmd, body, **kwargs):
            if cmd == 0x014a:
                client._on_data(None, encode(0x014a, 2, {
                    "code": 200, "path": body["path"], "size": len(data),
                    "offset": body["offset"], "block_size": 8000,
                    "total_blocks": 2, "remain_blocks": 2,
                }, 0x31))
                client._on_data(None, encode(0x014b, 3, raw_block(1, chunks[0])))
            elif cmd == 0x014b and body["code"] == 200 and chunks:
                chunks.pop(0)
                if chunks:
                    client._on_data(None, encode(0x014b, 4, raw_block(2, chunks[0])))

        client._send = send
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "raw.bin"
            result = await client.experimental.download_raw_file(
                "/emmc/audio/00000000000000", target)
            self.assertEqual(target.read_bytes(), data)
            self.assertEqual(result["bytes"], len(data))
            self.assertFalse(target.with_name("raw.bin.part").exists())


class FakeSocket:
    def __init__(self, content):
        self.content = io.BytesIO(content)
    def recv(self, size):
        return self.content.read(min(size, 3))


class USBTests(unittest.TestCase):
    def test_upload_chunks_and_optional_readback(self):
        storage = MemoryStorage()
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.bin"
            content = b"x" * 140000
            source.write_bytes(content)
            result = storage.upload(source, "/emmc/mindlink/test.bin", verify=True)
            self.assertTrue(result["readback_verified"])
            self.assertEqual(storage.files["/emmc/mindlink/test.bin"], content)
            self.assertLessEqual(max(storage.chunk_sizes), 65536)
            self.assertFalse(any(name.endswith(".part") for name in storage.files))
            with self.assertRaises(FileExistsError):
                storage.upload(source, "/emmc/mindlink/TEST.bin")

    def test_failed_readback_does_not_publish_or_remove_existing_files(self):
        storage = MemoryStorage()
        storage.corrupt = True
        storage.files["/emmc/mindlink/keep.txt"] = b"keep"
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.bin"
            source.write_bytes(b"new file")
            with self.assertRaises(DeviceError):
                storage.upload(source, "/emmc/mindlink/new.bin", verify=True)
        self.assertEqual(storage.files, {"/emmc/mindlink/keep.txt": b"keep"})

    def test_sync_read_handles_short_socket_reads(self):
        storage = USBStorage("TEST-SN")
        @contextmanager
        def sync(operation, path):
            self.assertEqual(operation, b"RECV")
            yield FakeSocket(b"DATA" + struct.pack("<I", 5) + b"hello" + b"DONE" + b"\0" * 4)
        storage._sync = sync
        self.assertEqual(b"".join(storage.read_chunks("/emmc/mindlink/a.txt")), b"hello")
        with self.assertRaises(DeviceError):
            recv_exact(FakeSocket(b"a"), 2)

    def test_hid_info_packet_and_report_padding(self):
        usb = A1USB(IDENTITY)
        class Hid:
            def write(self, report):
                self.report = report
                self.sent = False
                return len(report)
            def read(self, size, timeout):
                response = encode(400, 11, {"sn": "TEST-SN"}, 0x31, header=HID_HEADER)
                return list(b"\x01" + response.ljust(127, b"\0"))
        usb.device = Hid()
        self.assertEqual(usb.info()["sn"], "TEST-SN")
        self.assertEqual(len(usb.device.report), 128)
        self.assertEqual(usb.device.report[0], 1)


class MemoryStorage(USBStorage):
    """Small in-memory peer to test host upload behavior without device writes."""
    def __init__(self):
        super().__init__("TEST-SN")
        self.files, self.chunk_sizes, self.corrupt = {}, [], False

    def space(self):
        return {"free_bytes": 10 * 1024**3}

    def list(self, path):
        return [{"name": PurePath(name).name} for name in self.files]

    def stat(self, path):
        if path in self.files:
            return {"mode": 0o100600, "size": len(self.files[path])}
        return {"mode": 0, "size": 0}

    @contextmanager
    def _sync(self, operation, path):
        if operation != b"SEND":
            raise AssertionError(operation)
        owner = self
        target = path.rsplit(",", 1)[0]
        class Stream(FakeSocket):
            def sendall(self, data):
                tag, size = struct.unpack_from("<4sI", data)
                if tag == b"DATA":
                    if len(data) != size + 8:
                        raise AssertionError("SEND chunk length")
                    owner.chunk_sizes.append(size)
                    owner.files[target] = owner.files.get(target, b"") + data[8:]
                elif tag == b"DONE":
                    owner.files.setdefault(target, b"")
        yield Stream(b"OKAY" + b"\0" * 4)

    def read_chunks(self, path):
        value = self.files[path]
        if self.corrupt:
            value = b"wrong"
        for offset in range(0, len(value), 65536):
            yield value[offset:offset + 65536]

    def _shell(self, command):
        verb, *paths = shlex.split(command)
        if verb == "mv":
            self.files[paths[1]] = self.files.pop(paths[0])
        elif verb == "rm":
            self.files.pop(paths[0], None)
        else:
            raise AssertionError(command)


if __name__ == "__main__":
    unittest.main()
