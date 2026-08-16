from __future__ import annotations

import asyncio
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from a1_auth_test import FrameReceiver, make_frame, make_token  # noqa: E402
from extract_preferences import load_devices, mask, select_device  # noqa: E402


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


if __name__ == "__main__":
    unittest.main()
