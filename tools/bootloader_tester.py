#!/usr/bin/env python3
"""Probe or program the Mini VCU bootloader through DAPLink virtual CAN."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import sys
import time

from boot_image import APP_BASE, decode_version, verify_package
from uds_tester import Tester


def security_key(seed: int) -> int:
    mixed = seed ^ 0xC35A91E7
    rotated = ((mixed << 5) | (mixed >> 27)) & 0xFFFFFFFF
    return (rotated + 0x01020304) & 0xFFFFFFFF


def expect(response: bytes, prefix: bytes, label: str) -> None:
    if not response.startswith(prefix):
        raise RuntimeError(f"{label}: unexpected response {response.hex(' ')}")
    print(f"{label}: PASS  {response.hex(' ').upper()}")


def request_final(tester: Tester, request: bytes,
                  timeout: float = 2.0, pending_timeout: float = 20.0) -> bytes:
    response = tester.request(request, timeout)
    while len(response) == 3 and response[0] == 0x7F and response[2] == 0x78:
        print(f"0x{request[0]:02X}: response pending")
        response = tester.receive_response(pending_timeout)
    return response


def enter_bootloader(tester: Tester) -> None:
    tester.ignition = 0
    tester.gear = 0
    tester.wait_for_state(1, 3.0)
    print("Vehicle STANDBY: PASS")
    expect(tester.request(b"\x10\x02"), b"\x50\x02",
           "Application programming session")
    expect(tester.request(b"\x11\x01"), b"\x51\x01",
           "Programming reset accepted")
    tester.send_vehicle_commands = False
    time.sleep(0.35)
    tester.port.reset_input_buffer()


def unlock(tester: Tester, test_wrong_key: bool = False) -> None:
    expect(tester.request(b"\x10\x02"), b"\x50\x02",
           "Bootloader programming session")
    seed_response = tester.request(b"\x27\x01")
    expect(seed_response, b"\x67\x01", "Security seed")
    if len(seed_response) != 6:
        raise RuntimeError("security seed length is invalid")
    seed = int.from_bytes(seed_response[2:6], "big")
    key = security_key(seed)
    if test_wrong_key:
        expect(tester.request(b"\x27\x02" +
                              (key ^ 1).to_bytes(4, "big")),
               b"\x7F\x27\x35", "Wrong security key rejected")
    expect(tester.request(b"\x27\x02" + key.to_bytes(4, "big")),
           b"\x67\x02", "Security unlock")


def erase_application(tester: Tester) -> None:
    erase = request_final(tester, b"\x31\x01\xFF\x01",
                          pending_timeout=30.0)
    expect(erase, b"\x71\x01\xFF\x01\x00", "Erase sectors 5..10")


def request_download(tester: Tester, image_length: int) -> None:
    download = (b"\x34\x00\x44" + APP_BASE.to_bytes(4, "big") +
                image_length.to_bytes(4, "big"))
    expect(tester.request(download), b"\x74\x20\x01\x02",
           "RequestDownload")


def transfer_block(tester: Tester, block_sequence: int, chunk: bytes) -> None:
    response = tester.request(bytes([0x36, block_sequence]) + chunk,
                              timeout=4.0)
    if response != bytes([0x76, block_sequence]):
        raise RuntimeError(
            f"TransferData BSC=0x{block_sequence:02X}: "
            f"unexpected response {response.hex(' ')}")


def flash_package(tester: Tester, package_path: Path) -> None:
    manifest, image = verify_package(package_path.read_bytes())
    print(f"Package: {package_path}")
    print(f"Image: {len(image)} bytes, CRC32=0x{manifest.image_crc32:08X}, "
          f"version={decode_version(manifest.software_version)}")

    erase_application(tester)
    request_download(tester, len(image))

    block_sequence = 1
    sent = 0
    started = time.monotonic()
    for offset in range(0, len(image), 256):
        chunk = image[offset:offset + 256]
        transfer_block(tester, block_sequence, chunk)
        block_sequence = (block_sequence + 1) & 0xFF
        sent += len(chunk)
        if sent == len(image) or sent % 4096 == 0:
            print(f"TransferData: {sent}/{len(image)} bytes")

    transfer_exit = (b"\x37" + manifest.image_crc32.to_bytes(4, "big") +
                     manifest.software_version.to_bytes(4, "big"))
    expect(tester.request(transfer_exit, timeout=8.0), b"\x77",
           "TransferExit/CRC/manifest commit")
    elapsed = time.monotonic() - started
    print(f"PROGRAMMING PASS: {sent} bytes in {elapsed:.1f}s")


def interrupt_download(tester: Tester, package_path: Path) -> None:
    _, image = verify_package(package_path.read_bytes())
    erase_application(tester)
    request_download(tester, len(image))

    transfer_block(tester, 1, image[:256])
    wrong = tester.request(b"\x36\x03" + image[256:512])
    expect(wrong, b"\x7F\x36\x73", "Wrong block sequence rejected")

    block_sequence = 2
    for offset in range(256, 4096, 256):
        transfer_block(tester, block_sequence, image[offset:offset + 256])
        block_sequence = (block_sequence + 1) & 0xFF
    print("INTERRUPTION POINT REACHED: 4096 bytes written, Manifest absent")
    print("Now unplug and reconnect the board, then run --recover.")


def crc_rejection_test(tester: Tester, package_path: Path) -> None:
    manifest, image = verify_package(package_path.read_bytes())

    before_erase = (b"\x34\x00\x44" + APP_BASE.to_bytes(4, "big") +
                    len(image).to_bytes(4, "big"))
    expect(tester.request(before_erase), b"\x7F\x34\x24",
           "Interrupted image stayed in Bootloader")

    erase_application(tester)
    request_download(tester, 8)
    transfer_block(tester, 1, image[:8])
    wrong_crc = manifest.image_crc32 ^ 1
    transfer_exit = (b"\x37" + wrong_crc.to_bytes(4, "big") +
                     manifest.software_version.to_bytes(4, "big"))
    expect(tester.request(transfer_exit), b"\x7F\x37\x72",
           "Wrong image CRC rejected")


def run(args: argparse.Namespace) -> int:
    try:
        import serial
    except ImportError:
        print("pyserial is missing; install tools/requirements-sim.txt")
        return 2

    try:
        with serial.Serial(args.port, args.baud, timeout=0.005) as port:
            port.reset_input_buffer()
            tester = Tester(port)
            if args.recover:
                tester.send_vehicle_commands = False
            elif not args.already_in_bootloader:
                enter_bootloader(tester)
            else:
                tester.send_vehicle_commands = False
            unlock(tester, test_wrong_key=bool(args.interrupt_test))

            if args.probe:
                print("BOOTLOADER PROBE PASS (Flash was not erased)")
                print("Power-cycle the board to return to the application.")
                return 0

            if args.interrupt_test:
                interrupt_download(tester, args.interrupt_test)
                return 0
            if args.recover:
                crc_rejection_test(tester, args.recover)
                flash_package(tester, args.recover)
                print("INTERRUPTION/CRC RECOVERY PASS")
                return 0
            flash_package(tester, args.package)
            return 0
    except (OSError, TimeoutError, ValueError, RuntimeError) as exc:
        print(f"BOOTLOADER TEST FAIL: {exc}", file=sys.stderr)
        return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--probe", action="store_true",
                      help="enter and unlock bootloader without erasing Flash")
    mode.add_argument("--package", type=Path,
                      help="verified .kya package to program")
    mode.add_argument("--interrupt-test", type=Path,
                      help="erase and stop after 4096 bytes for a power-loss test")
    mode.add_argument("--recover", type=Path,
                      help="after power cycle, test CRC rejection and restore package")
    parser.add_argument("--already-in-bootloader", action="store_true",
                        help="skip application session/reset requests")
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
