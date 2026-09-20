import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from bootloader_tester import security_key  # noqa: E402
from uds_tester import isotp_request_frames  # noqa: E402


class BootloaderProtocolTests(unittest.TestCase):
    def test_security_key_reference(self):
        self.assertEqual(security_key(0x13579BDF), 0x02A34A1E)

    def test_single_frame_request(self):
        frames = isotp_request_frames(b"\x27\x01")
        self.assertEqual(frames, [b"\x02\x27\x01\x00\x00\x00\x00\x00"])

    def test_multiframe_request_round_trip(self):
        payload = bytes([0x36, 0xA5]) + bytes(range(256))
        frames = isotp_request_frames(payload)
        self.assertEqual(frames[0][:2], b"\x11\x02")
        rebuilt = bytearray(frames[0][2:])
        expected_sn = 1
        for frame in frames[1:]:
            self.assertEqual(frame[0], 0x20 | expected_sn)
            rebuilt.extend(frame[1:])
            expected_sn = (expected_sn + 1) & 0x0F
        self.assertEqual(bytes(rebuilt[:len(payload)]), payload)

    def test_download_request_length(self):
        request = (b"\x34\x00\x44" + (0x08020000).to_bytes(4, "big") +
                   (75284).to_bytes(4, "big"))
        self.assertEqual(len(request), 11)
        self.assertGreater(len(isotp_request_frames(request)), 1)


if __name__ == "__main__":
    unittest.main()
