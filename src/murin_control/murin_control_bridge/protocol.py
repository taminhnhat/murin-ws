"""Firmware telemetry decoded to the existing dashboard event contract."""

import math
import struct

NAMES = {
    0: "HEARTBEAT",
    1: "CMD_MOTOR",
    2: "CMD_SERVO",
    3: "TELEMETRY_BATTERY",
    4: "TELEMETRY_IMU",
    5: "TELEMETRY_DRIVE_STATE",
    0x10: "CMD_CONFIG",
    0x7E: "ACK",
    0x7F: "NACK",
}


def wheel_twist(command, separation):
    if not isinstance(command, dict):
        raise ValueError("cmd_vel must contain left and right wheel speeds")
    values = [command.get("left"), command.get("right")]
    if any(
        type(x) not in (int, float) or not math.isfinite(x) or abs(x) > 0.5
        for x in values
    ):
        raise ValueError("Wheel speeds must be finite numbers within +/-0.5 m/s")
    left, right = values
    return (left + right) / 2, (right - left) / separation


def decode_frame(data):
    data = bytes(data)
    if len(data) < 2:
        raise ValueError("Missing frame header")
    kind, seq = data[:2]
    payload = data[2:]
    message = dict(
        msgType=kind,
        msgName=NAMES.get(kind, f"0x{kind:02x}"),
        seq=seq,
        rawPayload=payload.hex(),
        telemetry=None,
    )
    imu = None
    if kind == 3:
        valid, status, stamp, *values = struct.unpack("<BBI4f", payload)
        if not all(map(math.isfinite, values)):
            raise ValueError("Non-finite battery telemetry")
        message["telemetry"] = dict(
            valid=bool(valid),
            status=status,
            timestampMs=stamp,
            **dict(zip(("voltage", "current", "power", "energy"), values)),
        )
    elif kind == 4:
        valid, status, stamp, *values = struct.unpack("<BBq13f", payload)
        if not all(map(math.isfinite, values)):
            raise ValueError("Non-finite IMU telemetry")
        imu = dict(
            valid=bool(valid),
            status=status,
            timestampUs=str(stamp),
            acceleration=values[:3],
            angularVelocity=values[3:6],
            magneticField=values[6:9],
            quaternion=values[9:13],
        )
    elif kind == 5:
        stamp, *values = struct.unpack("<I4f", payload)
        if not all(map(math.isfinite, values)):
            raise ValueError("Non-finite drive telemetry")
        message["driveState"] = dict(
            timestampMs=stamp,
            **dict(
                zip(
                    (
                        "linearVelocity",
                        "angularVelocity",
                        "leftVelocity",
                        "rightVelocity",
                    ),
                    values,
                )
            ),
        )
    return message, imu
