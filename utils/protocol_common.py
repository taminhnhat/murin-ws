#!/usr/bin/env python3
"""Shared protocol helpers for ESP32 USB framed-link communication."""

import struct


SOF = 0xAA
ESCAPE = 0x1B
ESCAPE_XOR = 0x20

MSG_HEARTBEAT = 0x00
MSG_CMD_MOTOR = 0x01
MSG_CMD_SERVO = 0x02
MSG_TELEMETRY_BATTERY = 0x03
MSG_TELEMETRY = MSG_TELEMETRY_BATTERY  # Backward-compatible name.
MSG_TELEMETRY_IMU = 0x04
MSG_TELEMETRY_DRIVE_STATE = 0x05
MSG_CMD_CONFIG = 0x10
MSG_SET_TIME = 0x11
MSG_DATA_IMU = 0x20
MSG_DATA_ENC = 0x21
MSG_ACK = 0x7E
MSG_NACK = 0x7F

TYPE_NAMES = {
    MSG_HEARTBEAT: "HEARTBEAT",
    MSG_CMD_MOTOR: "CMD_MOTOR",
    MSG_CMD_SERVO: "CMD_SERVO",
    MSG_TELEMETRY_BATTERY: "TELEMETRY_BATTERY",
    MSG_TELEMETRY_IMU: "TELEMETRY_IMU",
    MSG_TELEMETRY_DRIVE_STATE: "TELEMETRY_DRIVE_STATE",
    MSG_CMD_CONFIG: "CMD_CONFIG",
    MSG_SET_TIME: "SET_TIME",
    MSG_DATA_IMU: "DATA_IMU",
    MSG_DATA_ENC: "DATA_ENC",
    MSG_ACK: "ACK",
    MSG_NACK: "NACK",
}

ERR_OK = 0x00
ERR_CRC = 0x01
ERR_LEN = 0x02
ERR_TYPE = 0x03
ERR_CFG = 0x04
ERR_RANGE = 0x05

CFG_TELEM_ENABLE = 1
CFG_TELEM_RATE_MS = 2
CFG_TELEM_MASK = 3
CFG_TELEM_TIMEOUT_MS = 4

TELEM_MASK_BATTERY = 1 << 0
TELEM_MASK_IMU = 1 << 1
TELEM_MASK_DRIVE_STATE = 1 << 3

BATTERY_TELEMETRY_FORMAT = "<BBIffff"
IMU_TELEMETRY_FORMAT = "<BBq13f"
BATTERY_TELEMETRY_SIZE = struct.calcsize(BATTERY_TELEMETRY_FORMAT)
IMU_TELEMETRY_SIZE = struct.calcsize(IMU_TELEMETRY_FORMAT)
DRIVE_STATE_TELEMETRY_FORMAT = "<Iffff"
DRIVE_STATE_TELEMETRY_SIZE = struct.calcsize(DRIVE_STATE_TELEMETRY_FORMAT)

ERR_NAMES = {
    ERR_OK: "OK",
    ERR_CRC: "ERR_CRC",
    ERR_LEN: "ERR_LEN",
    ERR_TYPE: "ERR_TYPE",
    ERR_CFG: "ERR_CFG",
    ERR_RANGE: "ERR_RANGE",
}


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


def stuff(payload: bytes) -> bytes:
    out = bytearray()
    for b in payload:
        if b == SOF or b == ESCAPE:
            out.append(ESCAPE)
            out.append(b ^ ESCAPE_XOR)
        else:
            out.append(b)
    return bytes(out)


def unstuff(payload: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(payload):
        b = payload[i]
        if b == ESCAPE and i + 1 < len(payload):
            i += 1
            out.append(payload[i] ^ ESCAPE_XOR)
        else:
            out.append(b)
        i += 1
    return bytes(out)


def build_frame(msg_type: int, seq: int, payload: bytes) -> bytes:
    stuffed = stuff(payload)
    header = struct.pack("<BBH", msg_type, seq, len(stuffed))
    crc_val = crc16(header + stuffed)
    return bytes([SOF]) + header + stuffed + struct.pack("<H", crc_val)


def encode_motor(left_mps: float, right_mps: float) -> bytes:
    return struct.pack("<ff", left_mps, right_mps)


def encode_servo(channel: int, pulse_us: int) -> bytes:
    return struct.pack("<BH", channel, pulse_us)


def encode_config(key: int, value: int) -> bytes:
    return struct.pack("<Bi", key, value)


def encode_set_time(unix_seconds: int) -> bytes:
    return struct.pack("<Q", unix_seconds)


def decode_battery_telemetry(payload: bytes) -> dict:
    if len(payload) != BATTERY_TELEMETRY_SIZE:
        raise ValueError(
            f"battery telemetry must be {BATTERY_TELEMETRY_SIZE} bytes, "
            f"got {len(payload)}"
        )

    valid, status, timestamp, voltage, current, power, energy = struct.unpack(
        BATTERY_TELEMETRY_FORMAT, payload
    )
    return {
        "valid": bool(valid),
        "status": status,
        "timestamp": timestamp,
        "voltage": voltage,
        "current": current,
        "power": power,
        "energy": energy,
    }


def decode_imu_telemetry(payload: bytes) -> dict:
    if len(payload) != IMU_TELEMETRY_SIZE:
        raise ValueError(
            f"IMU telemetry must be {IMU_TELEMETRY_SIZE} bytes, got {len(payload)}"
        )

    values = struct.unpack(IMU_TELEMETRY_FORMAT, payload)
    return {
        "valid": bool(values[0]),
        "status": values[1],
        "timestamp_us": values[2],
        "acceleration_mps2": values[3:6],
        "angular_velocity_rad_s": values[6:9],
        "magnetic_field_uT": values[9:12],
        "quaternion_wxyz": values[12:16],
    }


def decode_drive_state_telemetry(payload: bytes) -> dict:
    if len(payload) != DRIVE_STATE_TELEMETRY_SIZE:
        raise ValueError(
            f"drive-state telemetry must be {DRIVE_STATE_TELEMETRY_SIZE} bytes, "
            f"got {len(payload)}"
        )

    timestamp_ms, linear_velocity, angular_velocity, left_velocity, right_velocity = (
        struct.unpack(DRIVE_STATE_TELEMETRY_FORMAT, payload)
    )
    return {
        "timestamp_ms": timestamp_ms,
        "linear_velocity": linear_velocity,
        "angular_velocity": angular_velocity,
        "left_velocity": left_velocity,
        "right_velocity": right_velocity,
    }


class FrameParser:
    """Stream-oriented parser: feed raw bytes, get complete frames back."""

    def __init__(self):
        self._buf = bytearray()

    def feed(self, data: bytes):
        self._buf.extend(data)
        return self._extract_frames()

    def _extract_frames(self):
        frames = []
        while True:
            try:
                start = self._buf.index(SOF)
            except ValueError:
                self._buf.clear()
                break

            if start > 0:
                self._buf = self._buf[start:]

            if len(self._buf) < 7:
                break

            msg_type = self._buf[1]
            seq = self._buf[2]
            length = struct.unpack_from("<H", self._buf, 3)[0]
            total = 1 + 1 + 1 + 2 + length + 2

            if len(self._buf) < total:
                break

            raw_payload = bytes(self._buf[5 : 5 + length])
            raw_crc = struct.unpack_from("<H", self._buf, 5 + length)[0]
            crc_data = bytes(self._buf[1 : 5 + length])
            computed = crc16(crc_data)

            self._buf = self._buf[total:]

            if computed != raw_crc:
                print(f"  [CRC FAIL] expected 0x{computed:04X} got 0x{raw_crc:04X}")
                continue

            payload = unstuff(raw_payload)
            frames.append((msg_type, seq, payload))

        return frames
