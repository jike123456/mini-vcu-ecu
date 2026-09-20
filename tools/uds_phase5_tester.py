#!/usr/bin/env python3
"""Regression for UDS sessions, DIDs, ResponsePending and ECU reset."""

from __future__ import annotations

import argparse
import struct
import sys
import time

from uds_tester import CAN_ID_REQUEST, CAN_ID_RESPONSE, Tester
from virtual_vcu import MSG_VCAN_TX


def pump(tester: Tester, seconds: float) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        tester.send_command_if_due()
        tester.receive_frames()


def expect_suppressed_response(tester: Tester, uds_payload: bytes,
                               window: float = 0.30) -> None:
    tester.send_can(CAN_ID_REQUEST, bytes([len(uds_payload)]) + uds_payload)
    deadline = time.monotonic() + window
    while time.monotonic() < deadline:
        tester.send_command_if_due()
        for message_id, _, payload in tester.receive_frames():
            if message_id != MSG_VCAN_TX or len(payload) != 11:
                continue
            can_id, _ = struct.unpack_from("<HB", payload)
            if can_id == CAN_ID_RESPONSE:
                raise ValueError("suppressPosRspMsgIndicationBit still produced a response")


def runtime_session(response: bytes) -> int:
    if len(response) != 11 or response[:3] != b"\x62\xF1\x00":
        raise ValueError(f"unexpected F100 response: {response.hex(' ')}")
    return response[10]


def uptime_ms(response: bytes) -> int:
    if len(response) != 7 or response[:3] != b"\x62\xF1\x01":
        raise ValueError(f"unexpected F101 response: {response.hex(' ')}")
    return int.from_bytes(response[3:7], "big")


def run(args) -> int:
    try:
        import serial
    except ImportError:
        print("pyserial is missing; install tools/requirements-sim.txt")
        return 2

    try:
        with serial.Serial(args.port, args.baud, timeout=0.005) as port:
            port.reset_input_buffer()
            tester = Tester(port)
            tester.ignition = 0
            tester.gear = 0
            tester.wait_for_state(1)

            session = tester.request(b"\x10\x03")
            if session != b"\x50\x03\x00\x32\x01\xF4":
                raise ValueError(f"bad 0x10 response: {session.hex(' ')}")
            print("0x10 EXTENDED SESSION/P2/P2*: PASS")

            present = tester.request(b"\x3E\x00")
            if present != b"\x7E\x00":
                raise ValueError(f"bad 0x3E response: {present.hex(' ')}")
            print("0x3E TESTER PRESENT: PASS")

            vin = tester.request(b"\x22\xF1\x90")
            if vin != b"\x62\xF1\x90KAOYA-F407-ECU001":
                raise ValueError(f"bad VIN DID: {vin!r}")
            print("0x22 F190 VIN MULTIFRAME: PASS")

            version = tester.request(b"\x22\xF1\x95")
            if version != b"\x62\xF1\x95VCU-0.6.0":
                raise ValueError(f"bad software DID: {version!r}")
            print("0x22 F195 SOFTWARE VERSION: PASS")

            vector_table = tester.request(b"\x22\xF1\x02")
            if vector_table != b"\x62\xF1\x02\x08\x02\x00\x00":
                raise ValueError(f"bad VTOR DID: {vector_table.hex(' ')}")
            print("0x22 F102 VTOR 0x08020000: PASS")

            runtime = tester.request(b"\x22\xF1\x00")
            if runtime_session(runtime) != 3:
                raise ValueError("F100 did not report extended session")
            print("0x22 F100 RUNTIME DATA: PASS")

            invalid_did = tester.request(b"\x22\x12\x34")
            if invalid_did != b"\x7F\x22\x31":
                raise ValueError(f"bad unsupported-DID NRC: {invalid_did.hex(' ')}")
            invalid_session = tester.request(b"\x10\x7F")
            if invalid_session != b"\x7F\x10\x12":
                raise ValueError(f"bad unsupported-session NRC: {invalid_session.hex(' ')}")
            print("NRC 0x31/0x12: PASS")

            pump(tester, 4.2)
            expect_suppressed_response(tester, b"\x3E\x80")
            pump(tester, 1.2)
            if runtime_session(tester.request(b"\x22\xF1\x00")) != 3:
                raise ValueError("suppressed TesterPresent did not refresh S3 timeout")
            print("0x3E SUPPRESS RESPONSE/S3 REFRESH: PASS")

            pump(tester, 5.2)
            if runtime_session(tester.request(b"\x22\xF1\x00")) != 1:
                raise ValueError("S3 timeout did not return to default session")
            print("S3 5s TIMEOUT -> DEFAULT SESSION: PASS")

            reset_default = tester.request(b"\x11\x01")
            if reset_default != b"\x7F\x11\x7E":
                raise ValueError(
                    f"ECU reset was not session-restricted: {reset_default.hex(' ')}")
            print("0x11 RESET IN DEFAULT -> NRC 0x7E: PASS")

            session = tester.request(b"\x10\x03")
            if session != b"\x50\x03\x00\x32\x01\xF4":
                raise ValueError(f"could not re-enter extended: {session.hex(' ')}")

            routine_started = time.monotonic()
            pending = tester.request(b"\x31\x01\xFF\x00")
            if pending != b"\x7F\x31\x78":
                raise ValueError(f"bad ResponsePending: {pending.hex(' ')}")
            final = tester.receive_response(timeout=2.0)
            routine_elapsed = time.monotonic() - routine_started
            if final != b"\x71\x01\xFF\x00\x00":
                raise ValueError(f"bad routine final response: {final.hex(' ')}")
            if routine_elapsed < 0.15 or routine_elapsed > 1.5:
                raise ValueError(f"unexpected routine duration: {routine_elapsed:.3f}s")
            print(f"0x31 ROUTINE 0xFF00: NRC 0x78 -> FINAL ({routine_elapsed:.3f}s): PASS")

            before_reset = uptime_ms(tester.request(b"\x22\xF1\x01"))
            print(f"0x22 F101 UPTIME BEFORE RESET: {before_reset} ms")
            reset = tester.request(b"\x11\x01")
            if reset != b"\x51\x01":
                raise ValueError(f"bad 0x11 positive response: {reset.hex(' ')}")
            print("0x11 POSITIVE RESPONSE BEFORE RESET: PASS")

            # The positive response is transmitted before the deferred reset.
            # Keep the virtual-CAN command stream alive while the ECU boots again.
            time.sleep(0.50)
            tester.port.reset_input_buffer()
            tester.parser = type(tester.parser)()
            tester.next_command = time.monotonic()
            tester.wait_for_state(1, timeout=3.0)
            after_reset = uptime_ms(tester.request(b"\x22\xF1\x01"))
            if after_reset >= before_reset:
                raise ValueError(
                    f"uptime did not roll back across reset: {before_reset}->{after_reset}")
            if runtime_session(tester.request(b"\x22\xF1\x00")) != 1:
                raise ValueError("ECU reset did not restore default diagnostic session")
            print(f"0x11 HARD RESET/UPTIME {before_reset}->{after_reset} ms: PASS")
            print("UDS PHASE5 REGRESSION PASS")
            return 0
    except (OSError, TimeoutError, ValueError) as exc:
        print(f"UDS Phase5 regression error: {exc}")
        return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    return run(parser.parse_args())


if __name__ == "__main__":
    sys.exit(main())
