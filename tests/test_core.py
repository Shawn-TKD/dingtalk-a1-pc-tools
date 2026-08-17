from __future__ import annotations

import asyncio
import http.client
import json
from pathlib import Path
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "console"))

from a1_auth_test import FrameReceiver, make_frame, make_token, parse_file_index  # noqa: E402
from a1_live_stream_probe import parse_stream_metadata, stream_control_body  # noqa: E402
from a1_memo_capture import make_ogg, parse_audio_push  # noqa: E402
from dtyj_to_ogg import extract_packets, extract_packets_with_markers  # noqa: E402
from extract_preferences import load_devices, mask, select_device  # noqa: E402
from h5_contract_scan import scan_paths  # noqa: E402
import server as console_server  # noqa: E402
from server import (  # noqa: E402
    access_token_matches,
    delete_request_body,
    parse_byte_range,
)


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

    def test_dtyj_extracts_marker_flags_and_extended_index(self):
        records = b"".join(
            prefix + bytes([0x4B]) * 80
            for prefix in (
                bytes.fromhex("00000000"),
                bytes.fromhex("a0000000"),
                bytes.fromhex("80010000"),
            )
        )
        packets, _sample_rate, _version, markers = extract_packets_with_markers(
            self._make_dtyj(records, 84)
        )
        self.assertEqual(len(packets), 3)
        self.assertEqual(
            markers,
            [
                {
                    "relative_seconds": 0.02,
                    "frame_index": 1,
                    "marker_index": 0,
                    "flag": "a0000000",
                    "source": "dtyj_frame_flag",
                },
                {
                    "relative_seconds": 0.04,
                    "frame_index": 2,
                    "marker_index": 1,
                    "flag": "80010000",
                    "source": "dtyj_frame_flag",
                },
            ],
        )

    def test_dtyj_rejects_extended_index_without_marker_bit(self):
        records = bytes.fromhex("20010000") + bytes([0x4B]) * 80
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

    def test_memo_parser_extracts_batched_packets_and_ignores_sentinel(self):
        payload = bytearray(28 + 168)
        payload[4:8] = (1700000000).to_bytes(4, "big")
        payload[16:20] = (12).to_bytes(4, "big")
        payload[20:24] = (168).to_bytes(4, "big")
        payload[28:] = bytes([0x4B]) * 168
        fid, block, packets = parse_audio_push(bytes(payload))
        self.assertEqual((fid, block), (1700000000, 12))
        self.assertEqual([len(packet) for packet in packets], [84, 84])

        sentinel = bytearray(32)
        sentinel[4:8] = (1700000000).to_bytes(4, "big")
        self.assertEqual(parse_audio_push(bytes(sentinel))[2], [])

    def test_memo_ogg_has_opus_headers_and_eos(self):
        content = make_ogg([bytes([0x4B]) * 84] * 2, 16000, 123)
        self.assertTrue(content.startswith(b"OggS"))
        self.assertIn(b"OpusHead", content)
        self.assertIn(b"OpusTags", content)
        self.assertEqual(content.count(b"OggS"), 4)

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


class ConsoleTests(unittest.TestCase):
    def test_parse_byte_range_supports_media_requests(self):
        self.assertIsNone(parse_byte_range(None, 100))
        self.assertEqual(parse_byte_range("bytes=0-9", 100), (0, 9))
        self.assertEqual(parse_byte_range("bytes=10-", 100), (10, 99))
        self.assertEqual(parse_byte_range("bytes=-10", 100), (90, 99))
        with self.assertRaises(ValueError):
            parse_byte_range("bytes=100-", 100)

    def test_delete_body_matches_official_string_fid_contract(self):
        self.assertEqual(
            delete_request_body("owned-device", 1700000000),
            {"did": "owned-device", "fid": "1700000000"},
        )

    def test_lan_access_token_requires_exact_match(self):
        token = "a-secure-random-token-value"
        self.assertTrue(access_token_matches(token, token))
        self.assertFalse(access_token_matches(token, "wrong-token"))
        self.assertTrue(access_token_matches("", ""))

class ConsoleHttpTests(unittest.TestCase):
    def setUp(self):
        self.temp_directory = tempfile.TemporaryDirectory()
        self.original_options = console_server.OPTIONS
        self.original_state = console_server.LAST_STATE
        self.original_delete = console_server.delete_recording
        self.original_summary = console_server.summarize_sync
        self.original_api_key = console_server.API_KEY_OVERRIDE
        self.fid = 1700000000
        self.token = "test-access-token-value-1234"
        self.delete_calls = []
        console_server.OPTIONS = SimpleNamespace(
            access_token=self.token,
            output_dir=self.temp_directory.name,
            api_key_file=str(Path(self.temp_directory.name) / ".api-key"),
            asr_model="test-asr",
            summary_model="test-summary",
        )
        console_server.LAST_STATE = {
            "connected": True,
            "device": None,
            "recordings": [{"fid": self.fid, "duration_seconds": 5}],
        }

        async def fake_delete(fid):
            self.delete_calls.append(fid)
            return {"code": 200}

        console_server.delete_recording = fake_delete
        self.server = console_server.ThreadingHTTPServer(
            ("127.0.0.1", 0), console_server.ConsoleHandler
        )
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        console_server.OPTIONS = self.original_options
        console_server.LAST_STATE = self.original_state
        console_server.delete_recording = self.original_delete
        console_server.summarize_sync = self.original_summary
        console_server.API_KEY_OVERRIDE = self.original_api_key
        self.temp_directory.cleanup()

    def request(self, method, path, body=None, token=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port)
        headers = {}
        if token:
            headers["X-A1-Access-Token"] = token
        if body is not None:
            body = json.dumps(body)
            headers["Content-Type"] = "application/json"
        connection.request(method, path, body=body, headers=headers)
        response = connection.getresponse()
        payload = json.loads(response.read().decode("utf-8"))
        connection.close()
        return response.status, payload

    def test_api_rejects_missing_access_token(self):
        status, payload = self.request("GET", "/api/state")
        self.assertEqual(status, 401)
        self.assertIn("访问令牌", payload["error"])

    def test_delete_without_backup_requires_explicit_confirmation(self):
        status, payload = self.request("POST", "/api/delete", {"fid": self.fid}, self.token)
        self.assertEqual(status, 500)
        self.assertIn("confirmation", payload["error"])
        self.assertEqual(self.delete_calls, [])

        request_body = {"fid": self.fid, "confirmed": True}
        status, payload = self.request("POST", "/api/delete", request_body, self.token)
        self.assertEqual(status, 200)
        self.assertEqual(payload["deleted_fid"], self.fid)
        self.assertEqual(self.delete_calls, [self.fid])
        self.assertEqual(payload["state"]["recordings"], [])

    def test_delete_with_backups_preserves_them_as_local_only(self):
        output_dir = Path(self.temp_directory.name)
        dtyj = output_dir / f"a1-{self.fid}.dtyj"
        ogg = output_dir / f"a1-{self.fid}.ogg"
        dtyj.write_bytes(b"DTYJ backup")
        ogg.write_bytes(b"OggS backup")
        request_body = {"fid": self.fid, "confirmed": True}
        status, payload = self.request("POST", "/api/delete", request_body, self.token)
        self.assertEqual(status, 200)
        self.assertEqual(payload["deleted_fid"], self.fid)
        self.assertEqual(self.delete_calls, [self.fid])
        self.assertTrue(dtyj.exists())
        self.assertTrue(ogg.exists())
        self.assertEqual(len(payload["state"]["recordings"]), 1)
        local_only = payload["state"]["recordings"][0]
        self.assertEqual(local_only["fid"], self.fid)
        self.assertFalse(local_only["on_device"])
        self.assertTrue(local_only["local_url"].endswith(f"a1-{self.fid}.ogg"))

    def test_local_memo_is_listed_and_deleted_with_its_metadata(self):
        fid = self.fid + 1
        output_dir = Path(self.temp_directory.name)
        ogg = output_dir / f"memo-{fid}.ogg"
        metadata = output_dir / f"memo-{fid}.json"
        ogg.write_bytes(b"OggS memo")
        metadata.write_text(
            json.dumps({"duration_seconds": 4.08, "transcription": "测试文本"}),
            encoding="utf-8",
        )

        status, state = self.request("GET", "/api/state", token=self.token)
        self.assertEqual(status, 200)
        memo = next(item for item in state["recordings"] if item["fid"] == fid)
        self.assertEqual(memo["kind"], "voice_memo")
        self.assertEqual(memo["transcription"], "测试文本")

        status, payload = self.request(
            "POST", "/api/delete", {"fid": fid, "confirmed": True}, self.token
        )
        self.assertEqual(status, 200)
        self.assertTrue(payload["local_only"])
        self.assertFalse(ogg.exists())
        self.assertFalse(metadata.exists())

    def test_existing_memo_state_keeps_its_local_audio_url(self):
        fid = self.fid + 2
        output_dir = Path(self.temp_directory.name)
        ogg = output_dir / f"memo-{fid}.ogg"
        metadata = output_dir / f"memo-{fid}.json"
        ogg.write_bytes(b"OggS memo")
        metadata.write_text(
            json.dumps({"duration_seconds": 3.2, "transcription": "稍后处理"}),
            encoding="utf-8",
        )
        console_server.LAST_STATE["recordings"].append(
            {"fid": fid, "kind": "voice_memo", "duration_seconds": 3.2, "on_device": False}
        )

        status, state = self.request("GET", "/api/state", token=self.token)

        self.assertEqual(status, 200)
        memo = next(item for item in state["recordings"] if item["fid"] == fid)
        self.assertTrue(memo["local_url"].endswith(f"memo-{fid}.ogg"))
        self.assertEqual(memo["transcription"], "稍后处理")

    def test_delete_local_copy_preserves_device_record(self):
        output_dir = Path(self.temp_directory.name)
        paths = [output_dir / f"a1-{self.fid}{suffix}" for suffix in (".dtyj", ".ogg", ".json")]
        for path in paths:
            path.write_bytes(b"{}" if path.suffix == ".json" else b"local copy")
        status, payload = self.request(
            "POST",
            "/api/delete-local",
            {"fid": self.fid, "kind": "recording", "confirmed": True},
            self.token,
        )
        self.assertEqual(status, 200)
        self.assertEqual(len(payload["removed_files"]), 3)
        self.assertTrue(all(not path.exists() for path in paths))
        item = next(record for record in payload["state"]["recordings"] if record["fid"] == self.fid)
        self.assertTrue(item["on_device"])
        self.assertIsNone(item["local_url"])

    def test_delete_local_only_record_removes_it_from_state(self):
        console_server.LAST_STATE = {"connected": False, "device": None, "recordings": []}
        output_dir = Path(self.temp_directory.name)
        (output_dir / f"a1-{self.fid}.ogg").write_bytes(b"OggS local")
        status, state = self.request("GET", "/api/state", token=self.token)
        self.assertEqual(status, 200)
        self.assertEqual([item["fid"] for item in state["recordings"]], [self.fid])
        status, payload = self.request(
            "POST",
            "/api/delete-local",
            {"fid": self.fid, "kind": "recording", "confirmed": True},
            self.token,
        )
        self.assertEqual(status, 200)
        self.assertEqual(payload["state"]["recordings"], [])

    def test_api_key_stays_private_and_summary_is_saved(self):
        output_dir = Path(self.temp_directory.name)
        (output_dir / f"a1-{self.fid}.ogg").write_bytes(b"OggS test")
        (output_dir / f"a1-{self.fid}.json").write_text(
            json.dumps({"transcription": "明天下午提交设计稿。"}), encoding="utf-8"
        )
        secret = "sk-this-key-must-never-be-returned"
        status, payload = self.request(
            "POST", "/api/settings/api-key", {"api_key": secret}, self.token
        )
        self.assertEqual(status, 200)
        self.assertTrue(payload["ai_key_configured"])
        self.assertNotIn(secret, json.dumps(payload))

        console_server.summarize_sync = lambda text, key, model: "## 摘要\n设计稿待提交"
        status, payload = self.request(
            "POST", "/api/summarize", {"fid": self.fid}, self.token
        )
        self.assertEqual(status, 200)
        self.assertEqual(payload["summary"], "## 摘要\n设计稿待提交")
        self.assertNotIn(secret, json.dumps(payload))
        metadata = json.loads((output_dir / f"a1-{self.fid}.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["summary_model"], "test-summary")


if __name__ == "__main__":
    unittest.main()
