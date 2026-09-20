#!/usr/bin/env python3
"""UART-backed virtual CAN peer for the Mini VCU firmware."""

from __future__ import annotations

import argparse
import struct
import sys
import time

SOF = b"\xAA\x55"
EOF = b"\x66\xBB"
VERSION = 1
MSG_VCAN_RX = 0x02
MSG_VCAN_TX = 0x82
CAN_ID_COMMAND = 0x100
CAN_ID_MOTION_STATUS = 0x180
STATE_NAMES = {0: "INIT", 1: "STANDBY", 2: "DRIVE", 3: "SAFE_STOP", 4: "FAULT"}


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def crc8_sae_j1850(data: bytes) -> int:
    crc = 0xFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1D) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc ^ 0xFF


def build_uart_frame(message_id: int, sequence: int, payload: bytes) -> bytes:
    body = struct.pack("<BBBBH", VERSION, message_id, 0, sequence & 0xFF, len(payload)) + payload
    return SOF + body + struct.pack("<H", crc16_ccitt_false(body)) + EOF


def build_command(vx_mmps: int, wz_mradps: int, ignition: int, gear: int,
                  alive: int, corrupt_crc: bool = False) -> bytes:
    can_data = bytearray(struct.pack("<hhBBB", vx_mmps, wz_mradps, ignition, gear, alive & 0x0F))
    can_data.append(crc8_sae_j1850(can_data))
    if corrupt_crc:
        can_data[7] ^= 0x01
    payload = struct.pack("<HB", CAN_ID_COMMAND, 8) + can_data
    return payload


class FrameParser:
    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, data: bytes):
        self.buffer.extend(data)
        frames = []
        while True:
            start = self.buffer.find(SOF)
            if start < 0:
                self.buffer[:] = self.buffer[-1:] if self.buffer[-1:] == SOF[:1] else b""
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < 12:
                break
            payload_length = struct.unpack_from("<H", self.buffer, 6)[0]
            frame_length = 12 + payload_length
            if payload_length > 64:
                del self.buffer[0]
                continue
            if len(self.buffer) < frame_length:
                break
            candidate = bytes(self.buffer[:frame_length])
            del self.buffer[:frame_length]
            if candidate[-2:] != EOF:
                continue
            expected = struct.unpack_from("<H", candidate, 8 + payload_length)[0]
            if crc16_ccitt_false(candidate[2:8 + payload_length]) != expected:
                continue
            frames.append((candidate[3], candidate[5], candidate[8:8 + payload_length]))
        return frames


def decode_motion_status(payload: bytes):
    if len(payload) != 11:
        raise ValueError("virtual CAN payload length is not 11")
    can_id, dlc = struct.unpack_from("<HB", payload)
    data = payload[3:11]
    if can_id != CAN_ID_MOTION_STATUS or dlc != 8:
        return None
    if crc8_sae_j1850(data[:7]) != data[7]:
        raise ValueError("0x180 CRC8 mismatch")
    left, right, state, fault, alive = struct.unpack("<hhBBB", data[:7])
    if alive & 0xF0:
        raise ValueError("0x180 alive reserved bits are not zero")
    return left, right, state, fault, alive


def self_test() -> None:
    assert crc16_ccitt_false(b"123456789") == 0x29B1
    assert crc8_sae_j1850(b"123456789") == 0x4B
    payload = build_command(300, -120, 1, 1, 5)
    assert len(payload) == 11
    frame = build_uart_frame(MSG_VCAN_RX, 7, payload)
    parsed = FrameParser().feed(frame)
    assert parsed == [(MSG_VCAN_RX, 7, payload)]
    status_data = bytearray(struct.pack("<hhBBB", 123, -45, 1, 0, 9))
    status_data.append(crc8_sae_j1850(status_data))
    decoded = decode_motion_status(struct.pack("<HB", CAN_ID_MOTION_STATUS, 8) + status_data)
    assert decoded == (123, -45, 1, 0, 9)
    print("SELF-TEST PASS: CRC16, CRC8, framing and 0x180 decode")


def list_ports() -> int:
    try:
        from serial.tools import list_ports as serial_list_ports
    except ImportError:
        print("pyserial is missing; run: python -m pip install -r tools/requirements-sim.txt")
        return 2
    ports = list(serial_list_ports.comports())
    if not ports:
        print("No serial ports found")
        return 1
    for port in ports:
        print(f"{port.device:8} {port.description}")
    return 0


def run(args: argparse.Namespace) -> int:
    try:
        import serial
    except ImportError:
        print("pyserial is missing; run: python -m pip install -r tools/requirements-sim.txt")
        return 2

    parser = FrameParser()
    serial_seq = 0
    alive = 0
    sent = 0
    received = 0
    bad_status = 0
    last_status_alive = None
    last_state_fault = None
    saw_drive = False
    saw_safe_stop = False
    saw_active_dtc = False
    saw_clean_recovery = False
    peak_wheel_speed = 0
    start = time.monotonic()
    next_tx = start
    stopped_notice = False

    print(f"Open {args.port} @ {args.baud}; TX 0x100 every {args.period_ms} ms")
    print("Press Ctrl+C to stop. state=2 is DRIVE; state=3/fault bit0 is 100 ms safe-stop.")
    try:
        with serial.Serial(args.port, args.baud, timeout=0.005) as port:
            while args.duration <= 0 or time.monotonic() - start < args.duration:
                now = time.monotonic()
                transmit_enabled = args.drop_after <= 0 or now - start < args.drop_after
                if transmit_enabled and now >= next_tx:
                    elapsed = now - start
                    periodic_corrupt = (args.bad_crc_every > 0 and
                                        (sent + 1) % args.bad_crc_every == 0)
                    window_corrupt = (args.bad_crc_from >= 0 and
                                      elapsed >= args.bad_crc_from and
                                      elapsed < args.bad_crc_from + args.bad_crc_duration)
                    corrupt = periodic_corrupt or window_corrupt
                    payload = build_command(args.vx, args.wz, args.ignition, args.gear, alive, corrupt)
                    port.write(build_uart_frame(MSG_VCAN_RX, serial_seq, payload))
                    serial_seq = (serial_seq + 1) & 0xFF
                    alive = (alive + 1) & 0x0F
                    sent += 1
                    next_tx += args.period_ms / 1000.0
                    if next_tx < now:
                        next_tx = now + args.period_ms / 1000.0
                elif not transmit_enabled and not stopped_notice:
                    print(f"--- injection stopped at {now - start:.3f}s; expecting safe-stop within 100 ms ---")
                    stopped_notice = True

                waiting = port.in_waiting
                chunk = port.read(waiting if waiting else 1)
                for message_id, _, payload in parser.feed(chunk):
                    if message_id != MSG_VCAN_TX:
                        continue
                    try:
                        status = decode_motion_status(payload)
                    except ValueError as exc:
                        bad_status += 1
                        print(f"RX reject: {exc}")
                        continue
                    if status is None:
                        continue
                    left, right, state, fault, status_alive = status
                    peak_wheel_speed = max(peak_wheel_speed, abs(left), abs(right))
                    if state == 2:
                        saw_drive = True
                    if state == 3:
                        saw_safe_stop = True
                    if fault & 0x02:
                        saw_active_dtc = True
                    if saw_active_dtc and state == 2 and (fault & 0x03) == 0:
                        saw_clean_recovery = True
                    if last_status_alive is not None and status_alive != ((last_status_alive + 1) & 0x0F):
                        print(f"WARN status alive jump {last_status_alive}->{status_alive}")
                    last_status_alive = status_alive
                    received += 1
                    state_fault = (state, fault)
                    if received == 1 or received % 25 == 0 or state_fault != last_state_fault:
                        state_name = STATE_NAMES.get(state, "UNKNOWN")
                        print(f"RX 0x180 n={received:<5} L={left:>6} R={right:>6} "
                              f"state={state}({state_name}) fault=0x{fault:02X} alive={status_alive}")
                    last_state_fault = state_fault
    except KeyboardInterrupt:
        print("\nStopped by user")
    except serial.SerialException as exc:
        print(f"Serial error: {exc}")
        return 2

    print(f"SUMMARY sent={sent} status_rx={received} bad_status={bad_status} "
          f"peak_wheel_speed={peak_wheel_speed}mm/s")
    if args.expect_dtc_cycle:
        speed_ok = peak_wheel_speed <= args.max_wheel_speed
        passed = saw_drive and saw_safe_stop and saw_active_dtc and saw_clean_recovery and speed_ok
        print("DTC-CYCLE " + ("PASS" if passed else "FAIL") +
              f": drive={saw_drive} safe={saw_safe_stop} "
              f"active_dtc={saw_active_dtc} recovery={saw_clean_recovery} "
              f"speed_ok={speed_ok} (peak={peak_wheel_speed}, limit={args.max_wheel_speed})")
        if not passed:
            return 1
    return 0 if received > 0 and bad_status == 0 else 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="MCU UART1 virtual COM port, e.g. COM9")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--vx", type=int, default=300, help="target longitudinal speed, mm/s")
    parser.add_argument("--wz", type=int, default=0, help="target yaw rate, mrad/s")
    parser.add_argument("--ignition", type=int, choices=(0, 1), default=1)
    parser.add_argument("--gear", type=int, choices=(0, 1, 2), default=1)
    parser.add_argument("--period-ms", type=int, default=20)
    parser.add_argument("--duration", type=float, default=8.0, help="seconds; <=0 runs forever")
    parser.add_argument("--drop-after", type=float, default=5.0,
                        help="stop command TX after N seconds but keep receiving; <=0 disables")
    parser.add_argument("--bad-crc-every", type=int, default=0,
                        help="corrupt every Nth command CAN CRC; 0 disables")
    parser.add_argument("--bad-crc-from", type=float, default=-1.0,
                        help="start a continuous bad-CRC window at N seconds; negative disables")
    parser.add_argument("--bad-crc-duration", type=float, default=1.0,
                        help="length of the continuous bad-CRC window in seconds")
    parser.add_argument("--expect-dtc-cycle", action="store_true",
                        help="require DRIVE->SAFE_STOP/DTC->clean DRIVE recovery")
    parser.add_argument("--max-wheel-speed", type=int, default=450,
                        help="maximum absolute wheel speed allowed by --expect-dtc-cycle, mm/s")
    parser.add_argument("--list-ports", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if not (args.self_test or args.list_ports or args.port):
        parser.error("--port is required unless --self-test or --list-ports is used")
    return args


def main() -> int:
    args = parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.list_ports:
        return list_ports()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
