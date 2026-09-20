import struct
import unittest

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from boot_image import (APP_BASE, APP_DATA_LIMIT, FLASH_BASE, IMAGE_MAGIC,
                        MANIFEST_SIZE, NVM_BASE, ImageValidationError,
                        create_factory_hex, create_package, crc32_ieee,
                        decode_version, encode_version, parse_intel_hex,
                        verify_factory_hex, verify_package)


def valid_image(size: int = 256) -> bytes:
    image = bytearray([0xA5] * size)
    struct.pack_into("<II", image, 0, 0x20020000, APP_BASE + 0x41)
    return bytes(image)


def valid_bootloader(size: int = 512) -> bytes:
    image = bytearray([0x5A] * size)
    struct.pack_into("<II", image, 0, 0x20000400, FLASH_BASE + 0x41)
    return bytes(image)


class BootImageTests(unittest.TestCase):
    def test_crc32_reference_vector(self):
        self.assertEqual(crc32_ieee(b"123456789"), 0xCBF43926)

    def test_package_round_trip(self):
        package = create_package(valid_image(), "0.6.1")
        manifest, image = verify_package(package)
        self.assertEqual(len(package), MANIFEST_SIZE + len(image))
        self.assertEqual(package[:4], struct.pack("<I", IMAGE_MAGIC))
        self.assertEqual(manifest.image_size, len(image))
        self.assertEqual(decode_version(manifest.software_version), "0.6.1.0")

    def test_corrupted_image_is_rejected(self):
        package = bytearray(create_package(valid_image(), "0.6.0"))
        package[-1] ^= 0x01
        with self.assertRaisesRegex(ImageValidationError, "application CRC32"):
            verify_package(bytes(package))

    def test_corrupted_manifest_is_rejected(self):
        package = bytearray(create_package(valid_image(), "0.6.0"))
        package[8] ^= 0x01
        with self.assertRaises(ImageValidationError):
            verify_package(bytes(package))

    def test_wrong_link_address_is_rejected(self):
        image = bytearray(valid_image())
        struct.pack_into("<I", image, 4, 0x08000041)
        with self.assertRaisesRegex(ImageValidationError, "outside"):
            create_package(bytes(image), "0.6.0")

    def test_manifest_overlap_is_rejected(self):
        oversized = valid_image(APP_DATA_LIMIT - APP_BASE + 1)
        with self.assertRaisesRegex(ImageValidationError, "overlaps"):
            create_package(oversized, "0.6.0")

    def test_version_component_range(self):
        with self.assertRaises(ImageValidationError):
            encode_version("1.256.0")

    def test_factory_hex_round_trip_and_preserves_nvm(self):
        factory = create_factory_hex(valid_bootloader(),
                                     create_package(valid_image(), "0.6.0"))
        manifest, image = verify_factory_hex(factory)
        memory = parse_intel_hex(factory)
        self.assertEqual(manifest.image_size, len(image))
        self.assertFalse(any(address >= NVM_BASE for address in memory))

    def test_factory_hex_checksum_corruption_is_rejected(self):
        lines = create_factory_hex(valid_bootloader(),
                                   create_package(valid_image(), "0.6.0")).splitlines()
        lines[1] = lines[1][:-2] + "00"
        with self.assertRaisesRegex(ImageValidationError, "corrupt"):
            verify_factory_hex("\n".join(lines))


if __name__ == "__main__":
    unittest.main()
