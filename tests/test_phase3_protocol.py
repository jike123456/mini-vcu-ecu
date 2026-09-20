import sys
import unittest
from pathlib import Path

import cantools

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import virtual_vcu as vcu  # noqa: E402


class Phase3ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.db = cantools.database.load_file(ROOT / "docs" / "mini_vcu.dbc")

    def test_crc_reference_vectors(self):
        self.assertEqual(vcu.crc16_ccitt_false(b"123456789"), 0x29B1)
        self.assertEqual(vcu.crc8_sae_j1850(b"123456789"), 0x4B)

    def test_command_frame_matches_dbc(self):
        payload = vcu.build_command(300, -120, 1, 1, 5)
        can_data = payload[3:]
        encoded = self.db.encode_message(
            "VCU_COMMAND",
            {
                "TargetVx": 300,
                "TargetWz": -120,
                "Ignition": 1,
                "Gear": 1,
                "AliveCounter": 5,
                "CRC8": can_data[7],
            },
        )
        self.assertEqual(encoded, can_data)

    def test_uart_parser_rejects_corrupt_outer_crc(self):
        payload = vcu.build_command(100, 0, 1, 1, 0)
        frame = bytearray(vcu.build_uart_frame(vcu.MSG_VCAN_RX, 0, payload))
        frame[-4] ^= 1
        self.assertEqual(vcu.FrameParser().feed(frame), [])

    def test_motion_status_crc_rejection(self):
        payload = bytearray.fromhex("80 01 08 7b 00 d3 ff 02 00 09 00")
        payload[-1] = vcu.crc8_sae_j1850(payload[3:10]) ^ 1
        with self.assertRaisesRegex(ValueError, "CRC8"):
            vcu.decode_motion_status(payload)


if __name__ == "__main__":
    unittest.main()
