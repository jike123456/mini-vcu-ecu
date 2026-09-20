#!/usr/bin/env python3
"""Inject ECU faults over virtual CAN and verify state, DTC and recovery."""

from __future__ import annotations

import argparse
import sys
import time

from uds_tester import Tester, parse_dtcs
from virtual_vcu import crc8_sae_j1850

FAULT_MASK_ALL = 0x1F
FAULT_CAN_ID = 0x101
EXPECTED_CODES = {0xC00500, 0xC00600, 0xC00700, 0xC00800, 0xC00900}


def injection_frame(mask: int, alive: int = 0) -> bytes:
    data = bytearray([mask & FAULT_MASK_ALL, 0xA5, 0, 0, 0, 0, alive & 0x0F])
    data.append(crc8_sae_j1850(data))
    return bytes(data)


def pump(tester: Tester, seconds: float) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        tester.send_command_if_due()
        tester.receive_frames()


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
            tester.wait_for_state(2)
            print("BASELINE DRIVE: PASS")

            tester.send_can(FAULT_CAN_ID, injection_frame(FAULT_MASK_ALL))
            tester.wait_for_state(4)
            pump(tester, 0.20)
            print("INJECT ALL -> FAULT: PASS")

            records = parse_dtcs(tester.request(b"\x19\x02\x2F"))
            codes = {code for code, _ in records}
            print("INJECTED DTC:",
                  ", ".join(f"{code:06X}/0x{status:02X}"
                            for code, status in records))
            missing = EXPECTED_CODES - codes
            if missing:
                print("Missing injected DTC:",
                      ", ".join(f"{code:06X}" for code in sorted(missing)))
                return 1

            tester.send_can(FAULT_CAN_ID, injection_frame(0, 1))
            tester.wait_for_state(2)
            pump(tester, 0.25)
            print("REMOVE INJECTION -> DRIVE: PASS")

            healed = parse_dtcs(tester.request(b"\x19\x02\x2F"))
            if any(status & 0x01 for _, status in healed
                   if _ in EXPECTED_CODES):
                print("Injected DTC did not heal current-failure status")
                return 1
            print("DTC DEBOUNCE/HEAL: PASS")

            rejected = tester.request(b"\x14\xFF\xFF\xFF")
            if rejected != b"\x7F\x14\x22":
                print("CLEAR while DRIVE: FAIL", rejected.hex(" "))
                return 1
            print("CLEAR while DRIVE: NRC 0x22 PASS")

            tester.ignition = 0
            tester.gear = 0
            tester.wait_for_state(1)
            cleared = tester.request(b"\x14\xFF\xFF\xFF")
            if cleared != b"\x54":
                print("CLEAR in STANDBY: FAIL", cleared.hex(" "))
                return 1
            pump(tester, args.persist_wait)
            print("CLEAR in STANDBY/NVM settle: PASS")
            print("FAULT INJECTION REGRESSION PASS")
            return 0
    except (OSError, TimeoutError, ValueError) as exc:
        print(f"Fault regression error: {exc}")
        return 2


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--persist-wait", type=float, default=5.5)
    return run(parser.parse_args())


if __name__ == "__main__":
    sys.exit(main())
