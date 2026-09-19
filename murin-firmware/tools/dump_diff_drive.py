#!/usr/bin/env python3
"""Send a deterministic differential-drive velocity trajectory to the controller.

The controller expects ``CMD_MOTOR`` payloads containing two little-endian
float32 values: left and right wheel velocities in metres per second.
"""

import argparse
import random
import sys
import time
from pathlib import Path

import serial
import yaml


# Keep this tool runnable from the repository root as well as from ``tools``.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "utils"))

from protocol_common import MSG_CMD_MOTOR, build_frame, encode_motor  # noqa: E402


CONFIG_PATH = Path(__file__).with_name("config.yaml")
MAX_INTERFACE_SPEED_MPS = 10.0
RAMP_MESSAGES = 6
MOTION_HOLD_SECONDS = 2.0
ZERO_HOLD_SECONDS = 1.0


def load_config() -> dict:
    try:
        with CONFIG_PATH.open("r", encoding="utf-8") as config_file:
            return yaml.safe_load(config_file) or {}
    except FileNotFoundError:
        return {}


def build_arg_parser() -> argparse.ArgumentParser:
    config = load_config()
    parser = argparse.ArgumentParser(
        description="Send a repeating positive/negative equal-wheel velocity trajectory."
    )
    parser.add_argument(
        "--port", default=config.get("port"), help="Serial port, e.g. COM11"
    )
    parser.add_argument(
        "--baudrate", type=int, default=config.get("baudrate", 2_000_000)
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=100.0,
        metavar="MILLISECONDS",
        help="Delay between commands in milliseconds (default: 100)",
    )
    parser.add_argument(
        "--max-velocity",
        type=float,
        default=0.5,
        metavar="MPS",
        help="Maximum absolute wheel velocity in m/s (default: 0.5)",
    )
    parser.add_argument(
        "--target-velocity",
        type=float,
        default=0.5,
        metavar="MPS",
        help="Peak absolute equal-wheel velocity (default: 0.5)",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=None,
        metavar="SECONDS",
        help="Stop after this many seconds; run until Ctrl+C when omitted",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="Send the initial trajectory sample and stop",
    )
    parser.add_argument(
        "--rand",
        action="store_true",
        help="Send independent random left/right velocities instead of the fixed trajectory",
    )
    return parser


def validate_args(args: argparse.Namespace, parser: argparse.ArgumentParser) -> None:
    if not args.port:
        parser.error(
            f"No serial port configured. Pass --port or set it in {CONFIG_PATH}."
        )
    if args.interval <= 0:
        parser.error("--interval must be greater than zero")
    if not 0 <= args.max_velocity <= MAX_INTERFACE_SPEED_MPS:
        parser.error(
            f"--max-velocity must be in the range 0..{MAX_INTERFACE_SPEED_MPS:g} m/s"
        )
    if args.duration is not None and args.duration <= 0:
        parser.error("--duration must be greater than zero")
    if abs(args.target_velocity) > args.max_velocity:
        parser.error("--target-velocity must be within --max-velocity")


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()
    validate_args(args, parser)
    sequence = 0
    started = time.monotonic()
    ser = None

    try:
        ser = serial.Serial(args.port, args.baudrate, timeout=0.05)
        ser.dtr = True
        ser.rts = False
        print(f"[SERIAL] Connected to {args.port} @ {args.baudrate}")

        peak_velocity = abs(args.target_velocity)
        interval_seconds = args.interval / 1000.0

        def ramp(start: float, end: float):
            for index in range(RAMP_MESSAGES):
                fraction = index / (RAMP_MESSAGES - 1)
                yield start + (end - start) * fraction

        def hold(velocity: float, seconds: float):
            for _ in range(max(1, round(seconds / interval_seconds))):
                yield velocity

        phases = (
            lambda: ramp(0.0, peak_velocity),
            lambda: hold(peak_velocity, MOTION_HOLD_SECONDS),
            lambda: ramp(peak_velocity, 0.0),
            lambda: hold(0.0, ZERO_HOLD_SECONDS),
            lambda: ramp(0.0, -peak_velocity),
            lambda: hold(-peak_velocity, MOTION_HOLD_SECONDS),
            lambda: ramp(-peak_velocity, 0.0),
            lambda: hold(0.0, ZERO_HOLD_SECONDS),
        )
        phase_index = 0
        while args.duration is None or time.monotonic() - started < args.duration:
            if args.rand:
                samples = (
                    (
                        random.uniform(-peak_velocity, peak_velocity),
                        random.uniform(-peak_velocity, peak_velocity),
                    ),
                )
            else:
                samples = (
                    (velocity, velocity)
                    for velocity in phases[phase_index % len(phases)]()
                )

            for left, right in samples:
                if (
                    args.duration is not None
                    and time.monotonic() - started >= args.duration
                ):
                    break
                ser.write(
                    build_frame(MSG_CMD_MOTOR, sequence, encode_motor(left, right))
                )
                print(
                    f"[TX] CMD_MOTOR seq={sequence} left={left:.3f} right={right:.3f}"
                )
                sequence = (sequence + 1) & 0xFF
                if args.once:
                    return 0
                time.sleep(interval_seconds)
            if not args.rand:
                phase_index += 1
    except KeyboardInterrupt:
        print("\n[SERIAL] Stopped")
    except serial.SerialException as exc:
        print(f"[SERIAL] {exc}", file=sys.stderr)
        return 1
    finally:
        # Always leave the drive stopped, including on Ctrl+C.
        if ser is not None and ser.is_open:
            ser.write(build_frame(MSG_CMD_MOTOR, sequence, encode_motor(0, 0)))
            ser.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
