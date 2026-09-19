#!/usr/bin/env python3
"""Monitor and send ESP32 framed-link / ROS2 serial messages."""

import argparse
import json
import queue
import struct
import sys
import threading
import time
import yaml
from pathlib import Path

import serial

CONFIG_PATH = Path(__file__).with_name("config.yaml")


def load_config():
    try:
        with CONFIG_PATH.open("r", encoding="utf-8") as f:
            return yaml.safe_load(f) or {}
    except FileNotFoundError:
        return {}


# Add utils directory to path
sys.path.insert(0, str(Path(__file__).parent.parent / "utils"))

from protocol_common import (
    CFG_TELEM_ENABLE,
    CFG_TELEM_MASK,
    CFG_TELEM_RATE_MS,
    CFG_TELEM_TIMEOUT_MS,
    ERR_NAMES,
    MSG_ACK,
    MSG_CMD_CONFIG,
    MSG_CMD_MOTOR,
    MSG_CMD_SERVO,
    MSG_HEARTBEAT,
    MSG_NACK,
    MSG_TELEMETRY,
    TYPE_NAMES,
    FrameParser,
    build_frame,
    encode_config,
    encode_motor,
    encode_servo,
)


CONFIG_KEY_NAMES = {
    CFG_TELEM_ENABLE: "TELEM_ENABLE",
    CFG_TELEM_RATE_MS: "TELEM_RATE_MS",
    CFG_TELEM_MASK: "TELEM_MASK",
    CFG_TELEM_TIMEOUT_MS: "TELEM_TIMEOUT_MS",
}

TELEMETRY_STRUCT = struct.Struct("<BBIffffHH")


def hex_bytes(data: bytes, max_len: int = 32) -> str:
    if not data:
        return "-"
    text = data[:max_len].hex(" ")
    if len(data) > max_len:
        text += f" ... +{len(data) - max_len} bytes"
    return text


def parse_u8(value: str) -> int:
    parsed = int(value, 0)
    if parsed < 0 or parsed > 0xFF:
        raise argparse.ArgumentTypeError("value must fit in uint8 range 0..255")
    return parsed


def parse_hex_payload(value: str) -> bytes:
    cleaned = value.replace("0x", "").replace("0X", "")
    cleaned = "".join(ch for ch in cleaned if not ch.isspace() and ch not in ":-_,")
    if len(cleaned) % 2:
        raise argparse.ArgumentTypeError(
            "hex payload must contain an even number of digits"
        )
    try:
        return bytes.fromhex(cleaned)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid hex payload: {exc}") from exc


def maybe_decode_text(payload: bytes) -> str | None:
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError:
        return None

    stripped = text.strip()
    if not stripped:
        return None

    if stripped[0] in "{[":
        try:
            return "json=" + json.dumps(json.loads(stripped), separators=(",", ":"))
        except json.JSONDecodeError:
            return f"text={stripped!r}"

    if all(ch.isprintable() or ch.isspace() for ch in stripped):
        return f"text={stripped!r}"
    return None


def decode_telemetry(payload: bytes) -> str:
    if len(payload) != TELEMETRY_STRUCT.size:
        return f"bad_len={len(payload)} raw={hex_bytes(payload)}"

    valid, err, timestamp, voltage, current, power, energy, voltage_raw, current_raw = (
        TELEMETRY_STRUCT.unpack(payload)
    )
    status = "valid" if valid else "invalid"
    return (
        f"{status} err={err} timestamp={timestamp}ms "
        f"voltage={voltage:.3f}V current={current:.3f}A "
        f"power={power:.3f}W energy={energy:.3f}Wh "
        f"raw_v={voltage_raw} raw_i={current_raw}"
    )


def decode_frame(msg_type: int, seq: int, payload: bytes) -> str:
    name = TYPE_NAMES.get(msg_type, f"0x{msg_type:02X}")

    if msg_type == MSG_TELEMETRY:
        return f"[RX] {name} seq={seq} {decode_telemetry(payload)}"

    if msg_type == MSG_ACK:
        ack_seq = payload[0] if payload else None
        return f"[RX] {name} seq={seq} ack_seq={ack_seq} raw={hex_bytes(payload)}"

    if msg_type == MSG_NACK:
        nack_seq = payload[0] if len(payload) >= 1 else None
        err = payload[1] if len(payload) >= 2 else None
        err_name = ERR_NAMES.get(err, f"0x{err:02X}") if err is not None else "missing"
        return f"[RX] {name} seq={seq} nack_seq={nack_seq} err={err_name} raw={hex_bytes(payload)}"

    if msg_type == MSG_HEARTBEAT:
        return f"[RX] {name} seq={seq}"

    if msg_type == MSG_CMD_MOTOR and len(payload) == 8:
        left, right = struct.unpack("<ff", payload)
        return f"[RX] {name} seq={seq} left={left:.3f} m/s right={right:.3f} m/s"

    if msg_type == MSG_CMD_SERVO and len(payload) == 3:
        channel, pulse = struct.unpack("<BH", payload)
        return f"[RX] {name} seq={seq} channel={channel} pulse={pulse}"

    if msg_type == MSG_CMD_CONFIG and len(payload) == 5:
        key, value = struct.unpack("<Bi", payload)
        key_name = CONFIG_KEY_NAMES.get(key, f"0x{key:02X}")
        return f"[RX] {name} seq={seq} key={key_name}({key}) value={value}"

    decoded_text = maybe_decode_text(payload)
    if decoded_text is not None:
        return f"[RX] {name} seq={seq} len={len(payload)} {decoded_text}"

    return f"[RX] {name} seq={seq} len={len(payload)} raw={hex_bytes(payload)}"


class Ros2UsbMonitor:
    def __init__(self, port: str, baudrate: int, raw: bool = False):
        self.port = port
        self.baudrate = baudrate
        self.raw = raw
        self.parser = FrameParser()
        self.rx_queue = queue.Queue()
        self.stop_event = threading.Event()
        self.seq = 0
        self.ser = None
        self.telemetry_count = 0
        self.telemetry_first_time = None
        self.telemetry_last_time = None

    def open(self):
        self.ser = serial.Serial(self.port, self.baudrate, timeout=0.05)
        self.ser.dtr = True
        # RTS is connected to the board reset/boot circuitry. Keep it low so
        # opening the CDC port does not put the ESP32 into download mode.
        self.ser.rts = False
        print(f"[SERIAL] Connected to {self.port} @ {self.baudrate}")

    def close(self):
        self.stop_event.set()
        if self.ser is not None:
            self.ser.close()

    def _next_seq(self) -> int:
        seq = self.seq
        self.seq = (self.seq + 1) & 0xFF
        return seq

    def send(self, msg_type: int, payload: bytes = b""):
        seq = self._next_seq()
        frame = build_frame(msg_type, seq, payload)
        self.ser.write(frame)
        name = TYPE_NAMES.get(msg_type, f"0x{msg_type:02X}")
        print(f"[TX] {name} seq={seq} payload={hex_bytes(payload)}")

    def start(self):
        thread = threading.Thread(target=self._read_loop, daemon=True)
        thread.start()
        return thread

    def _read_loop(self):
        while not self.stop_event.is_set():
            try:
                read_size = self.ser.in_waiting or 1
                data = self.ser.read(read_size)
            except serial.SerialException as exc:
                print(f"[SERIAL] Read error: {exc}", file=sys.stderr)
                self.stop_event.set()
                return

            if not data:
                continue

            if self.raw:
                print(f"[RAW] {hex_bytes(data, max_len=128)}")

            for frame in self.parser.feed(data):
                self.rx_queue.put(frame)

    def print_forever(self):
        while not self.stop_event.is_set():
            try:
                msg_type, seq, payload = self.rx_queue.get(timeout=0.2)
            except queue.Empty:
                continue
            if msg_type == MSG_TELEMETRY:
                now = time.monotonic()
                if self.telemetry_first_time is None:
                    self.telemetry_first_time = now
                self.telemetry_last_time = now
                self.telemetry_count += 1
            print(decode_frame(msg_type, seq, payload))

    def telemetry_rate_report(self) -> str:
        if self.telemetry_count == 0:
            return "[STATS] Telemetry frames=0 rate=0.00 Hz"

        if (
            self.telemetry_count == 1
            or self.telemetry_first_time == self.telemetry_last_time
        ):
            return "[STATS] Telemetry frames=1 rate=n/a"

        duration = self.telemetry_last_time - self.telemetry_first_time
        rate = (self.telemetry_count - 1) / duration
        period_ms = 1000.0 / rate
        return (
            f"[STATS] Telemetry frames={self.telemetry_count} "
            f"duration={duration:.2f}s rate={rate:.2f} Hz period={period_ms:.1f} ms"
        )


def build_arg_parser() -> argparse.ArgumentParser:
    config = load_config()

    parser = argparse.ArgumentParser(
        description="Read and send ESP32 framed-link serial messages."
    )
    parser.add_argument(
        "--port", default=config.get("port"), help="Serial port, e.g. COM11"
    )
    parser.add_argument(
        "--baudrate",
        type=float,
        default=config.get("baudrate", 2_000_000),
        help="Serial baudrate",
    )
    parser.add_argument(
        "--raw", action="store_true", help="Also print raw serial bytes"
    )
    parser.add_argument(
        "--heartbeat",
        action="store_true",
        help="Send one HEARTBEAT frame after opening the port",
    )
    parser.add_argument(
        "--motor",
        nargs=2,
        type=float,
        metavar=("LEFT_MPS", "RIGHT_MPS"),
        help="Send one CMD_MOTOR frame after opening the port",
    )
    parser.add_argument(
        "--servo",
        nargs=2,
        type=int,
        metavar=("CHANNEL", "PULSE_US"),
        help="Send one CMD_SERVO frame after opening the port",
    )
    parser.add_argument(
        "--config",
        nargs=2,
        type=int,
        metavar=("KEY", "VALUE"),
        help="Send one CMD_CONFIG frame after opening the port",
    )
    parser.add_argument(
        "--frame",
        nargs=2,
        metavar=("TYPE", "HEX_PAYLOAD"),
        help="Send one generic framed-link message. TYPE accepts decimal/hex; payload is hex bytes.",
    )
    parser.add_argument(
        "--json",
        nargs=2,
        metavar=("TYPE", "JSON_TEXT"),
        help="Send one generic framed-link message with UTF-8 JSON payload.",
    )
    return parser


def main() -> int:
    args = build_arg_parser().parse_args()
    if not args.port:
        print(
            f"No serial port configured. Pass --port or set it in {CONFIG_PATH}.",
            file=sys.stderr,
        )
        return 2

    monitor = Ros2UsbMonitor(args.port, args.baudrate, raw=args.raw)

    try:
        monitor.open()
        monitor.start()

        if args.heartbeat:
            monitor.send(MSG_HEARTBEAT)
        if args.motor is not None:
            monitor.send(MSG_CMD_MOTOR, encode_motor(args.motor[0], args.motor[1]))
        if args.servo is not None:
            monitor.send(MSG_CMD_SERVO, encode_servo(args.servo[0], args.servo[1]))
        if args.config is not None:
            monitor.send(MSG_CMD_CONFIG, encode_config(args.config[0], args.config[1]))
        if args.frame is not None:
            monitor.send(parse_u8(args.frame[0]), parse_hex_payload(args.frame[1]))
        if args.json is not None:
            json_type = parse_u8(args.json[0])
            json_payload = json.dumps(
                json.loads(args.json[1]), separators=(",", ":")
            ).encode("utf-8")
            monitor.send(json_type, json_payload)

        print("[SERIAL] Listening. Press Ctrl+C to stop.")
        monitor.print_forever()
    except KeyboardInterrupt:
        print("\n[SERIAL] Stopped")
    except serial.SerialException as exc:
        print(f"[SERIAL] {exc}", file=sys.stderr)
        return 1
    finally:
        monitor.close()
        time.sleep(0.05)
        print(monitor.telemetry_rate_report())

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
