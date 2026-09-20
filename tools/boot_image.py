#!/usr/bin/env python3
"""Create and verify Mini VCU bootloader update packages.

The .kya container is a 32-byte little-endian manifest followed by the raw
application image. The image itself must already be linked for APP_BASE.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import struct
import sys
import zlib

FLASH_BASE = 0x08000000
FLASH_END = 0x08100000
BOOTLOADER_END = 0x08020000
APP_BASE = BOOTLOADER_END
MANIFEST_ADDRESS = 0x080DFFE0
APP_DATA_LIMIT = MANIFEST_ADDRESS
NVM_BASE = 0x080E0000

SRAM_BASE = 0x20000000
SRAM_END = 0x20020000

IMAGE_MAGIC = 0x4159414B  # bytes: 4B 41 59 41, "KAYA"
MANIFEST_VERSION = 1
MANIFEST_FORMAT = "<8I"
MANIFEST_SIZE = struct.calcsize(MANIFEST_FORMAT)


class ImageValidationError(ValueError):
    """Raised when an update package cannot be safely accepted."""


def crc32_ieee(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def encode_version(text: str) -> int:
    parts = text.split(".")
    if not 1 <= len(parts) <= 4:
        raise ImageValidationError("version must contain 1 to 4 dot-separated bytes")
    try:
        values = [int(part, 10) for part in parts]
    except ValueError as exc:
        raise ImageValidationError("version components must be decimal integers") from exc
    if any(value < 0 or value > 255 for value in values):
        raise ImageValidationError("version components must be in range 0..255")
    values.extend([0] * (4 - len(values)))
    return ((values[0] << 24) | (values[1] << 16) |
            (values[2] << 8) | values[3])


def decode_version(value: int) -> str:
    return ".".join(str((value >> shift) & 0xFF)
                    for shift in (24, 16, 8, 0))


def validate_vector_table(image: bytes) -> tuple[int, int]:
    if len(image) < 8:
        raise ImageValidationError("image is too short to contain a vector table")
    if len(image) > APP_DATA_LIMIT - APP_BASE:
        raise ImageValidationError("image overlaps the reserved manifest/NvM area")

    initial_sp, reset_vector = struct.unpack_from("<II", image)
    if not (SRAM_BASE < initial_sp <= SRAM_END) or (initial_sp & 0x3):
        raise ImageValidationError(f"invalid initial stack pointer 0x{initial_sp:08X}")
    if (reset_vector & 1) == 0:
        raise ImageValidationError("reset vector is not a Thumb address")
    reset_address = reset_vector & ~1
    if not (APP_BASE <= reset_address < APP_BASE + len(image)):
        raise ImageValidationError(
            f"reset vector 0x{reset_vector:08X} is outside the application image")
    return initial_sp, reset_vector


def validate_bootloader_vector_table(image: bytes) -> tuple[int, int]:
    if len(image) < 8 or len(image) > BOOTLOADER_END - FLASH_BASE:
        raise ImageValidationError("bootloader size is outside its partition")
    initial_sp, reset_vector = struct.unpack_from("<II", image)
    if not (SRAM_BASE < initial_sp <= SRAM_END) or (initial_sp & 0x3):
        raise ImageValidationError("bootloader initial stack pointer is invalid")
    reset_address = reset_vector & ~1
    if ((reset_vector & 1) == 0 or
            not FLASH_BASE <= reset_address < FLASH_BASE + len(image)):
        raise ImageValidationError("bootloader reset vector is invalid")
    return initial_sp, reset_vector


@dataclass(frozen=True)
class Manifest:
    image_size: int
    image_crc32: int
    software_version: int
    flags: int = 0

    def pack(self) -> bytes:
        prefix = struct.pack(
            "<7I", IMAGE_MAGIC, MANIFEST_VERSION, APP_BASE, self.image_size,
            self.image_crc32, self.software_version, self.flags)
        return prefix + struct.pack("<I", crc32_ieee(prefix))

    @classmethod
    def unpack(cls, raw: bytes) -> "Manifest":
        if len(raw) != MANIFEST_SIZE:
            raise ImageValidationError("manifest must be exactly 32 bytes")
        fields = struct.unpack(MANIFEST_FORMAT, raw)
        magic, format_version, image_start = fields[:3]
        if magic != IMAGE_MAGIC:
            raise ImageValidationError("manifest magic is invalid")
        if format_version != MANIFEST_VERSION:
            raise ImageValidationError("manifest format version is unsupported")
        if image_start != APP_BASE:
            raise ImageValidationError("manifest application address is invalid")
        if crc32_ieee(raw[:28]) != fields[7]:
            raise ImageValidationError("manifest CRC32 is invalid")
        return cls(fields[3], fields[4], fields[5], fields[6])


def create_package(image: bytes, version: str, flags: int = 0) -> bytes:
    validate_vector_table(image)
    manifest = Manifest(len(image), crc32_ieee(image), encode_version(version), flags)
    return manifest.pack() + image


def verify_package(package: bytes) -> tuple[Manifest, bytes]:
    if len(package) < MANIFEST_SIZE + 8:
        raise ImageValidationError("package is truncated")
    manifest = Manifest.unpack(package[:MANIFEST_SIZE])
    image = package[MANIFEST_SIZE:]
    if len(image) != manifest.image_size:
        raise ImageValidationError("package length does not match the manifest")
    validate_vector_table(image)
    if crc32_ieee(image) != manifest.image_crc32:
        raise ImageValidationError("application CRC32 is invalid")
    return manifest, image


def _hex_record(address: int, record_type: int, data: bytes) -> str:
    body = bytes([len(data), (address >> 8) & 0xFF, address & 0xFF,
                  record_type]) + data
    checksum = (-sum(body)) & 0xFF
    return ":" + (body + bytes([checksum])).hex().upper()


def emit_intel_hex(segments: list[tuple[int, bytes]]) -> str:
    """Emit sparse Intel HEX without filling gaps between Flash partitions."""
    lines: list[str] = []
    active_upper = None
    previous_end = None
    for start, data in sorted(segments):
        if not data:
            continue
        if start < FLASH_BASE or start + len(data) > FLASH_END:
            raise ImageValidationError("factory segment is outside internal Flash")
        if previous_end is not None and start < previous_end:
            raise ImageValidationError("factory image segments overlap")
        previous_end = start + len(data)
        offset = 0
        while offset < len(data):
            absolute = start + offset
            upper = absolute >> 16
            if upper != active_upper:
                lines.append(_hex_record(0, 0x04, upper.to_bytes(2, "big")))
                active_upper = upper
            boundary = 0x10000 - (absolute & 0xFFFF)
            count = min(16, len(data) - offset, boundary)
            lines.append(_hex_record(absolute & 0xFFFF, 0x00,
                                     data[offset:offset + count]))
            offset += count
    lines.append(_hex_record(0, 0x01, b""))
    return "\n".join(lines) + "\n"


def parse_intel_hex(text: str) -> dict[int, int]:
    memory: dict[int, int] = {}
    upper = 0
    saw_eof = False
    for line_number, raw_line in enumerate(text.splitlines(), 1):
        line = raw_line.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise ImageValidationError(f"Intel HEX line {line_number} has no colon")
        try:
            record = bytes.fromhex(line[1:])
        except ValueError as exc:
            raise ImageValidationError(
                f"Intel HEX line {line_number} is not hexadecimal") from exc
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 0xFF:
            raise ImageValidationError(f"Intel HEX line {line_number} is corrupt")
        length = record[0]
        address = (record[1] << 8) | record[2]
        record_type = record[3]
        data = record[4:4 + length]
        if record_type == 0x00:
            absolute = upper + address
            for index, value in enumerate(data):
                location = absolute + index
                if location in memory:
                    raise ImageValidationError("Intel HEX contains overlapping data")
                memory[location] = value
        elif record_type == 0x01:
            saw_eof = True
            break
        elif record_type == 0x04 and length == 2:
            upper = int.from_bytes(data, "big") << 16
        else:
            raise ImageValidationError(
                f"Intel HEX record type 0x{record_type:02X} is unsupported")
    if not saw_eof:
        raise ImageValidationError("Intel HEX EOF record is missing")
    return memory


def create_factory_hex(bootloader: bytes, package: bytes) -> str:
    validate_bootloader_vector_table(bootloader)
    manifest, image = verify_package(package)
    return emit_intel_hex([
        (FLASH_BASE, bootloader),
        (APP_BASE, image),
        (MANIFEST_ADDRESS, manifest.pack()),
    ])


def verify_factory_hex(text: str) -> tuple[Manifest, bytes]:
    memory = parse_intel_hex(text)

    def read_range(start: int, length: int) -> bytes:
        try:
            return bytes(memory[start + offset] for offset in range(length))
        except KeyError as exc:
            raise ImageValidationError(
                f"factory image is missing address 0x{int(exc.args[0]):08X}") from exc

    boot_addresses = [address for address in memory
                      if FLASH_BASE <= address < BOOTLOADER_END]
    if not boot_addresses or min(boot_addresses) != FLASH_BASE:
        raise ImageValidationError("factory image has no bootloader")
    bootloader = read_range(FLASH_BASE, max(boot_addresses) - FLASH_BASE + 1)
    validate_bootloader_vector_table(bootloader)

    manifest = Manifest.unpack(read_range(MANIFEST_ADDRESS, MANIFEST_SIZE))
    image = read_range(APP_BASE, manifest.image_size)
    verify_package(manifest.pack() + image)
    if any(address >= NVM_BASE for address in memory):
        raise ImageValidationError("factory image writes into DTC NvM")
    return manifest, image


def print_summary(manifest: Manifest, image: bytes) -> None:
    initial_sp, reset_vector = struct.unpack_from("<II", image)
    print(f"application : 0x{APP_BASE:08X}..0x{APP_BASE + len(image) - 1:08X}")
    print(f"image size  : {manifest.image_size} bytes")
    print(f"image CRC32 : 0x{manifest.image_crc32:08X}")
    print(f"version     : {decode_version(manifest.software_version)}")
    print(f"initial SP  : 0x{initial_sp:08X}")
    print(f"reset vector: 0x{reset_vector:08X}")
    print("BOOT IMAGE VERIFY PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    pack_parser = subparsers.add_parser("pack", help="package a relocated .bin")
    pack_parser.add_argument("--input", required=True, type=Path)
    pack_parser.add_argument("--output", required=True, type=Path)
    pack_parser.add_argument("--version", required=True)

    verify_parser = subparsers.add_parser("verify", help="verify a .kya package")
    verify_parser.add_argument("--input", required=True, type=Path)

    factory_parser = subparsers.add_parser(
        "factory", help="combine bootloader and application into Intel HEX")
    factory_parser.add_argument("--bootloader", required=True, type=Path)
    factory_parser.add_argument("--application", required=True, type=Path)
    factory_parser.add_argument("--output", required=True, type=Path)
    factory_parser.add_argument("--manifest-output", type=Path,
                                help="also write the 32-byte manifest binary")

    factory_verify_parser = subparsers.add_parser(
        "verify-factory", help="verify a combined factory Intel HEX")
    factory_verify_parser.add_argument("--input", required=True, type=Path)

    args = parser.parse_args()
    try:
        if args.command == "pack":
            image = args.input.read_bytes()
            package = create_package(image, args.version)
            args.output.write_bytes(package)
            manifest, verified_image = verify_package(package)
            print_summary(manifest, verified_image)
            print(f"package     : {args.output}")
        elif args.command == "verify":
            manifest, image = verify_package(args.input.read_bytes())
            print_summary(manifest, image)
        elif args.command == "factory":
            package = args.application.read_bytes()
            factory = create_factory_hex(args.bootloader.read_bytes(), package)
            args.output.write_text(factory, encoding="ascii", newline="\n")
            if args.manifest_output is not None:
                manifest, _ = verify_package(package)
                args.manifest_output.write_bytes(manifest.pack())
            manifest, image = verify_factory_hex(factory)
            print_summary(manifest, image)
            print(f"factory HEX: {args.output}")
        else:
            manifest, image = verify_factory_hex(
                args.input.read_text(encoding="ascii"))
            print_summary(manifest, image)
        return 0
    except (OSError, ImageValidationError) as exc:
        print(f"BOOT IMAGE VERIFY FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
