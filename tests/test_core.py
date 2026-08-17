from __future__ import annotations

import asyncio
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from a1_auth_test import FrameReceiver, make_frame, make_token, parse_file_index  # noqa: E402
from a1_live_stream_probe import parse_stream_metadata, stream_control_body  # noqa: E402
from dtyj_to_ogg import extract_packets  # noqa: E402
from extract_preferences import load_devices, mask, select_device  # noqa: E402
from h5_contract_scan import scan_paths  # noqa: E402


class ProtocolTests(unittest.TestCase):
    def test_make_frame_uses_big_endian_command_and_length(self):
        frame = make_frame(0x0133, 0x11, {"a": 1})
        self.assertEqual(frame[:8], bytes.fromhex("1301331100000007"))
        self.assertEqual(frame[8:], b'{"a":1}')

    def test_receiver_reassembles_fragmented_frame(self):
        receiver = FrameReceiver()
        frame = make_frame(0x0132, 0x22, {"code": 200}, frame_type=0x31)
        receiver.on_notification(None, bytearray(frame[:5]))
        receiver.on_notification(None, bytearray(frame[5:]))

        async def receive():
            return await receiver.wait_json(0x0132, 0.5)

        self.assertEqual(asyncio.run(receive()), {"code": 200})

    def test_token_vector(self):
        token = make_token(
            "0123456789abcdef0123456789abcdef",
            "00000000000000000000000000000000",
        )
        self.assertEqual(
            token,
            "2828efe22aacb2e1f9af33660ae2a746e8427749de950998174bf02aa7f77199",
        )

    def test_parse_file_index_uses_eight_byte_records(self):
        payload = (
            bytes.fromhex("00c80002")
            + bytes.fromhex("00056553f1000005")
            + bytes.fromhex("000b6553f1010009")
            + bytes.fromhex("000000005a5a5a5a")
        )
        result = parse_file_index(payload)
        self.assertEqual(result["declared_count"], 2)
        self.assertEqual(result["records"][0]["fid"], 1700000000)
        self.assertEqual(result["records"][1]["status_or_duration"], 9)
        self.assertEqual(result["tail_bytes"], 8)

    def test_dtyj_accepts_known_high_bit_frame_flags(self):
        records = b"".join(
            bytes([flag, 0, 0, 0]) + bytes([0x4B]) * 80
            for flag in (0, 0x20, 0xC0)
        )
        container = self._make_dtyj(records, 84)
        packets, sample_rate, version = extract_packets(container)
        self.assertEqual(len(packets), 3)
        self.assertEqual(sample_rate, 16000)
        self.assertEqual(version, "v1.7")

    def test_dtyj_rejects_low_reserved_flag_bits(self):
        records = bytes([0x01, 0, 0, 0]) + bytes([0x4B]) * 80
        with self.assertRaisesRegex(ValueError, "unexpected record prefix"):
            extract_packets(self._make_dtyj(records, 84))

    def test_stream_parser_returns_metadata_without_audio(self):
        payload = bytearray(116)
        payload[4:8] = (1700000000).to_bytes(4, "big")
        payload[16:20] = (7).to_bytes(4, "big")
        payload[20:24] = (84).to_bytes(4, "big")
        payload[28:112] = bytes([0x4B]) * 84
        payload[112:116] = b"ZZZZ"
        result = parse_stream_metadata(bytes(payload))
        self.assertEqual(result["block_sequence"], 7)
        self.assertEqual(result["opus_tocs"], [0x4B])
        self.assertEqual(result["opus_units"], 1)
        self.assertNotIn("audio", result)

    def test_stream_parser_splits_batched_84_byte_units(self):
        payload = bytearray(28 + 252 + 4)
        payload[4:8] = (1700000000).to_bytes(4, "big")
        payload[16:20] = (8).to_bytes(4, "big")
        payload[20:24] = (252).to_bytes(4, "big")
        payload[28:280] = bytes([0x4B]) * 252
        payload[28 + 84] = 0x48
        payload[28 + 168] = 0x4B
        payload[280:284] = b"ZZZZ"
        result = parse_stream_metadata(bytes(payload))
        self.assertEqual(result["opus_tocs"], [0x4B, 0x48, 0x4B])
        self.assertEqual(result["opus_units"], 3)

    def test_stream_control_includes_did_added_by_official_wrapper(self):
        self.assertEqual(
            stream_control_body("owned-device", True),
            {
                "did": "owned-device",
                "action": "set",
                "params": [{"key": "upload_stream", "val": 1}],
            },
        )

    @staticmethod
    def _make_dtyj(records: bytes, record_size: int) -> bytes:
        import struct

        fmt = bytearray(48)
        fmt[:4] = b"fmt "
        struct.pack_into("<I", fmt, 12, 16000)
        struct.pack_into("<H", fmt, 24, record_size)
        data = b"data" + struct.pack("<I", len(records)) + b"\x00\x00\x00\x00" + records
        body = b"DTYJ" + b"ver " + b"v1.7" + bytes(fmt) + data
        return b"BABA" + struct.pack("<I", len(body)) + body


class PreferenceTests(unittest.TestCase):
    def test_load_and_select_device_without_printing_secret(self):
        device = {
            "sn": "SERIAL-EXAMPLE-0001",
            "deviceId": 123456,
            "corpId": "ding-example",
            "deviceSecret": "0123456789abcdef0123456789abcdef",
        }
        payload = json.dumps([device]).replace("&", "&amp;")
        xml = f'<map><string name="example_device_list_id">{payload}</string></map>'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PreferenceUtils.xml"
            path.write_text(xml, encoding="utf-8")
            devices = load_devices(path)
        self.assertEqual(select_device(devices, device["sn"])["deviceId"], 123456)
        self.assertNotIn(device["deviceSecret"], mask(device["deviceSecret"]))


class H5ContractTests(unittest.TestCase):
    def test_scan_reports_contracts_and_hosts_without_source(self):
        source = (
            'dd.internal.dinger.realStreamOperation({operationName:"set",'
            'operationParam:{needAsr:"start"}}); fetch("https://example.test/private?q=1")'
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bundle.js"
            path.write_text(source, encoding="utf-8")
            result = scan_paths([path])
        self.assertEqual(
            result["contracts"]["internal.dinger.realStreamOperation"]["count"], 1
        )
        self.assertEqual(result["url_hosts"], {"example.test": 1})
        self.assertFalse(result["source_snippets_included"])
        self.assertNotIn("private", json.dumps(result))


if __name__ == "__main__":
    unittest.main()
