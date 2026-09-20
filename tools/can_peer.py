#!/usr/bin/env python3
"""PC-side CAN peer for the STM32 physical-bus regression test."""

from __future__ import annotations

import argparse
import sys
import time

import can


REQUEST_ID = 0x5A0
RESPONSE_ID = 0x5A1


def auto_int(value: str) -> int:
    return int(value, 0)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Reply to STM32 CAN ID 0x5A0 frames on ID 0x5A1."
    )
    parser.add_argument(
        "--interface",
        default="slcan",
        help="python-can interface, for example slcan, pcan or vector",
    )
    parser.add_argument(
        "--channel",
        required=True,
        help="adapter channel, for example COM10 or PCAN_USBBUS1",
    )
    parser.add_argument("--bitrate", type=int, default=500000)
    parser.add_argument("--request-id", type=auto_int, default=REQUEST_ID)
    parser.add_argument("--response-id", type=auto_int, default=RESPONSE_ID)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rx_count = 0
    tx_count = 0

    try:
        bus = can.Bus(
            interface=args.interface,
            channel=args.channel,
            bitrate=args.bitrate,
            receive_own_messages=False,
        )
    except Exception as exc:  # Adapter-specific exceptions share no stable base.
        print(f"CAN open failed: {exc}", file=sys.stderr)
        return 2

    print(
        f"CAN peer ready: {args.interface}/{args.channel} "
        f"{args.bitrate} bit/s, 0x{args.request_id:03X} -> 0x{args.response_id:03X}"
    )

    try:
        while True:
            message = bus.recv(timeout=1.0)
            if message is None:
                continue
            if message.is_extended_id or message.arbitration_id != args.request_id:
                continue
            if len(message.data) != 8:
                print(
                    f"Ignore 0x{message.arbitration_id:03X}: DLC={len(message.data)}"
                )
                continue

            rx_count += 1
            response = can.Message(
                arbitration_id=args.response_id,
                is_extended_id=False,
                data=bytes(message.data),
            )
            bus.send(response, timeout=0.5)
            tx_count += 1
            timestamp = time.strftime("%H:%M:%S")
            payload = " ".join(f"{byte:02X}" for byte in message.data)
            print(
                f"[{timestamp}] RX#{rx_count} 0x{args.request_id:03X} {payload} | "
                f"TX#{tx_count} 0x{args.response_id:03X}"
            )
    except KeyboardInterrupt:
        print("\nCAN peer stopped.")
    except can.CanError as exc:
        print(f"CAN I/O failed: {exc}", file=sys.stderr)
        return 3
    finally:
        bus.shutdown()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
