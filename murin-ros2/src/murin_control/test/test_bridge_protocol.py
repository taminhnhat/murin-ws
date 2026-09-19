import math
import struct
import pytest
from murin_control_bridge.protocol import decode_frame, wheel_twist


def test_units_and_bounds():
    assert wheel_twist({"left": 0.1, "right": 0.3}, 0.2) == pytest.approx((0.2, 1.0))
    for value in ("0.2", math.nan, math.inf, 0.51, True, None):
        with pytest.raises(ValueError):
            wheel_twist({"left": value, "right": 0}, 0.2)


def test_telemetry():
    battery, imu = decode_frame(
        bytes([3, 255]) + struct.pack("<BBI4f", 1, 0, 42, 12, 1, 12, 3)
    )
    assert battery["telemetry"]["voltage"] == 12
    assert imu is None
    message, imu = decode_frame(
        bytes([4, 0]) + struct.pack("<BBq13f", 1, 0, 2**54, *range(13))
    )
    assert imu["timestampUs"] == str(2**54)
    assert imu["quaternion"] == [9, 10, 11, 12]
    message, _ = decode_frame(bytes([5, 1]) + struct.pack("<I4f", 42, 0.2, 1, 0.1, 0.3))
    assert message["driveState"]["rightVelocity"] == pytest.approx(0.3)
    with pytest.raises(struct.error):
        decode_frame(bytes([3, 0, 1]))
    with pytest.raises(ValueError):
        decode_frame(bytes([3, 0]) + struct.pack("<BBI4f", 1, 0, 42, math.nan, 1, 2, 3))
