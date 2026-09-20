#!/usr/bin/env python3
"""UDS/ISO-TP regression tester for the UART-backed virtual CAN link."""

from __future__ import annotations

import argparse
import struct
import sys
import time

from virtual_vcu import (CAN_ID_MOTION_STATUS, FrameParser, MSG_VCAN_RX,
                         MSG_VCAN_TX, build_command, build_uart_frame,
                         decode_motion_status)

CAN_ID_REQUEST = 0x7E0
CAN_ID_RESPONSE = 0x7E8


def isotp_request_frames(payload: bytes) -> list[bytes]:
    """Encode a UDS request into Classical-CAN ISO-TP frames."""
    if not 1 <= len(payload) <= 0xFFF:
        raise ValueError("ISO-TP request length must be 1..4095 bytes")
    if len(payload) <= 7:
        return [(bytes([len(payload)]) + payload).ljust(8, b"\x00")]

    frames = [bytes([0x10 | (len(payload) >> 8), len(payload) & 0xFF]) +
              payload[:6]]
    offset = 6
    sequence = 1
    while offset < len(payload):
        chunk = payload[offset:offset + 7]
        frames.append((bytes([0x20 | sequence]) + chunk).ljust(8, b"\x00"))
        offset += len(chunk)
        sequence = (sequence + 1) & 0x0F
    return frames


class Tester:
    def __init__(self, port, period_ms: int = 20,
                 send_vehicle_commands: bool = True) -> None:
        self.port = port
        self.period = period_ms / 1000.0
        self.parser = FrameParser()
        self.serial_seq = 0
        self.alive = 0
        self.next_command = time.monotonic()
        self.ignition = 1
        self.gear = 1
        self.last_state = None
        self.send_vehicle_commands = send_vehicle_commands

    def send_payload(self, payload: bytes) -> None:
        self.port.write(build_uart_frame(MSG_VCAN_RX, self.serial_seq, payload))
        self.serial_seq = (self.serial_seq + 1) & 0xFF

    def send_can(self, can_id: int, data: bytes) -> None:
        if len(data) > 8:
            raise ValueError("Classical CAN payload exceeds 8 bytes")
        payload = struct.pack("<HB", can_id, 8) + data.ljust(8, b"\x00")
        self.send_payload(payload)

    def send_command_if_due(self) -> None:
        if not self.send_vehicle_commands:
            return
        now = time.monotonic()
        if now < self.next_command:
            return
        payload = build_command(300 if self.ignition else 0, 0,
                                self.ignition, self.gear, self.alive)
        self.send_payload(payload)
        self.alive = (self.alive + 1) & 0x0F
        self.next_command += self.period
        if self.next_command < now:
            self.next_command = now + self.period

    def receive_frames(self):
        waiting = self.port.in_waiting
        chunk = self.port.read(waiting if waiting else 1)
        return self.parser.feed(chunk)

    def wait_for_state(self, expected: int, timeout: float = 2.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.send_command_if_due()
            for message_id, _, payload in self.receive_frames():
                if message_id != MSG_VCAN_TX:
                    continue
                status = decode_motion_status(payload)
                if status is not None:
                    self.last_state = status[2]
                    if status[2] == expected:
                        return
        raise TimeoutError(f"vehicle state {expected} not observed; last={self.last_state}")

    def request(self, uds_payload: bytes, timeout: float = 2.0) -> bytes:
        frames = isotp_request_frames(uds_payload)
        self.send_can(CAN_ID_REQUEST, frames[0])
        if len(frames) > 1:
            self.wait_for_flow_control(timeout)
            for frame in frames[1:]:
                self.send_can(CAN_ID_REQUEST, frame)

        return self.receive_response(timeout)

    def wait_for_flow_control(self, timeout: float = 2.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.send_command_if_due()
            for message_id, _, payload in self.receive_frames():
                if message_id != MSG_VCAN_TX or len(payload) != 11:
                    continue
                can_id, dlc = struct.unpack_from("<HB", payload)
                data = payload[3:11]
                if can_id != CAN_ID_RESPONSE or dlc != 8:
                    continue
                if data[0] == 0x30:
                    return
                raise ValueError(f"unexpected ISO-TP flow control: {data.hex(' ')}")
        raise TimeoutError("ISO-TP flow-control timeout")

    def receive_response(self, timeout: float = 2.0) -> bytes:
        """Receive one ISO-TP response without transmitting a new request."""

        deadline = time.monotonic() + timeout
        response = bytearray()
        total_length = None
        next_sn = 1
        while time.monotonic() < deadline:
            self.send_command_if_due()
            for message_id, _, payload in self.receive_frames():
                if message_id != MSG_VCAN_TX or len(payload) != 11:
                    continue
                can_id, dlc = struct.unpack_from("<HB", payload)
                data = payload[3:11]
                if can_id == CAN_ID_MOTION_STATUS:
                    try:
                        status = decode_motion_status(payload)
                        if status is not None:
                            self.last_state = status[2]
                    except ValueError:
                        pass
                    continue
                if can_id != CAN_ID_RESPONSE or dlc != 8:
                    continue

                pci = data[0] & 0xF0
                if pci == 0x00:
                    length = data[0] & 0x0F
                    return bytes(data[1:1 + length])
                if pci == 0x10:
                    total_length = ((data[0] & 0x0F) << 8) | data[1]
                    response.extend(data[2:8])
                    self.send_can(CAN_ID_REQUEST, b"\x30\x00\x00")
                    continue
                if pci == 0x20 and total_length is not None:
                    sn = data[0] & 0x0F
                    if sn != next_sn:
                        raise ValueError(f"ISO-TP sequence mismatch {sn}!={next_sn}")
                    next_sn = (next_sn + 1) & 0x0F
                    response.extend(data[1:8])
                    if len(response) >= total_length:
                        return bytes(response[:total_length])
        raise TimeoutError("UDS response timeout")


def parse_dtcs(response: bytes):
    if len(response) < 3 or response[:2] != b"\x59\x02":
        raise ValueError(f"unexpected ReadDTC response: {response.hex(' ')}")
    if (len(response) - 3) % 4:
        raise ValueError("malformed DTC response length")
    records = []
    for offset in range(3, len(response), 4):
        code = int.from_bytes(response[offset:offset + 3], "big")
        records.append((code, response[offset + 3]))
    return records


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

            if args.read_only:
                tester.ignition = 0
                tester.gear = 0
                tester.wait_for_state(1)
                records = parse_dtcs(tester.request(b"\x19\x02\x2F"))
                print("READ-ONLY DTC:",
                      ", ".join(f"{code:06X}/0x{status:02X}"
                                for code, status in records) or "none")
                passed = (not args.expect_empty) or (not records)
                print("UDS READ-ONLY " + ("PASS" if passed else "FAIL"))
                return 0 if passed else 1

            tester.wait_for_state(2)
            print("STATE DRIVE: PASS")

            before = parse_dtcs(tester.request(b"\x19\x02\x2F"))
            print("READ DTC before clear:",
                  ", ".join(f"{code:06X}/0x{status:02X}" for code, status in before)
                  or "none")
            if not before:
                print("Expected at least one stored DTC from the prior fault injection")
                return 1

            rejected = tester.request(b"\x14\xFF\xFF\xFF")
            if rejected != b"\x7F\x14\x22":
                print("CLEAR while DRIVE: FAIL", rejected.hex(" "))
                return 1
            print("CLEAR while DRIVE: correctly rejected with NRC 0x22")

            tester.ignition = 0
            tester.gear = 0
            tester.wait_for_state(1)
            print("STATE STANDBY: PASS")

            cleared = tester.request(b"\x14\xFF\xFF\xFF")
            if cleared != b"\x54":
                print("CLEAR in STANDBY: FAIL", cleared.hex(" "))
                return 1
            print("CLEAR in STANDBY: positive response 0x54")

            after = parse_dtcs(tester.request(b"\x19\x02\x2F"))
            print("READ DTC after clear:",
                  ", ".join(f"{code:06X}/0x{status:02X}" for code, status in after)
                  or "none")
            if after:
                return 1

            deadline = time.monotonic() + args.persist_wait
            while time.monotonic() < deadline:
                tester.send_command_if_due()
                tester.receive_frames()
            print(f"NVM settle wait: {args.persist_wait:.1f}s")
            print("UDS REGRESSION PASS")
            return 0
    except (OSError, TimeoutError, ValueError) as exc:
        print(f"UDS regression error: {exc}")
        return 2


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--persist-wait", type=float, default=5.5,
                        help="keep STANDBY alive so NvM can persist the clear")
    parser.add_argument("--read-only", action="store_true",
                        help="only enter STANDBY and read DTCs")
    parser.add_argument("--expect-empty", action="store_true",
                        help="with --read-only, fail if any matching DTC remains")
    return run(parser.parse_args())


if __name__ == "__main__":
    sys.exit(main())
