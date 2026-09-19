"""Opt-in cross-repository test: pytest this file with Socket.IO dependencies installed.

Uses only an allocated pseudo-terminal and an ephemeral HTTP port.
"""

import binascii
import os
from pathlib import Path
import select
import signal
import struct
import subprocess
import threading
import time
import tty

import socketio
import rclpy
from geometry_msgs.msg import TwistStamped
from std_msgs.msg import Bool
from std_srvs.srv import Trigger
from sensor_msgs.msg import JointState


def wire(kind, seq, payload):
    stuffed = bytearray()
    for byte in payload:
        stuffed.extend((0x1B, byte ^ 0x20) if byte in (0xAA, 0x1B) else (byte,))
    body = struct.pack("<BBH", kind, seq, len(stuffed)) + stuffed
    return b"\xaa" + body + struct.pack("<H", binascii.crc_hqx(body, 0xFFFF))


class Firmware:
    def __init__(self):
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)  # Disable PTY echo before the emulator transmits.
        self.port = os.ttyname(self.slave)
        self.stop = threading.Event()
        self.telemetry = True
        self.commands = []
        self.thread = threading.Thread(target=self.run)
        self.thread.start()

    def run(self):
        buffer = bytearray()
        wheels = (0.0, 0.0)
        seq = 0
        started = time.monotonic()
        while not self.stop.is_set():
            if select.select([self.master], [], [], 0.02)[0]:
                buffer.extend(os.read(self.master, 4096))
            while len(buffer) >= 7:
                if buffer[0] != 0xAA:
                    del buffer[0]
                    continue
                size = int.from_bytes(buffer[3:5], "little")
                if len(buffer) < size + 7:
                    break
                frame = bytes(buffer[: size + 7])
                del buffer[: size + 7]
                if binascii.crc_hqx(frame[1:-2], 0xFFFF) != int.from_bytes(
                    frame[-2:], "little"
                ):
                    continue
                payload = bytearray()
                escape = False
                for byte in frame[5:-2]:
                    if escape:
                        payload.append(byte ^ 0x20)
                        escape = False
                    elif byte == 0x1B:
                        escape = True
                    else:
                        payload.append(byte)
                if frame[1] == 1:
                    wheels = struct.unpack("<ff", payload)
                    self.commands.append((time.monotonic(), wheels))
                os.write(self.master, wire(0x7E, frame[2], bytes([frame[2]])))
            if self.telemetry:
                stamp = int((time.monotonic() - started) * 1000)
                os.write(
                    self.master,
                    wire(
                        5, seq, struct.pack("<I4f", stamp, sum(wheels) / 2, 0, *wheels)
                    ),
                )
                os.write(
                    self.master,
                    wire(3, seq, struct.pack("<BBI4f", 1, 0, stamp, 12, 1, 12, 2)),
                )
                os.write(
                    self.master,
                    wire(
                        4,
                        seq,
                        struct.pack(
                            "<BBq13f", 1, 0, stamp * 1000, *([0] * 9 + [1, 0, 0, 0])
                        ),
                    ),
                )
                seq = (seq + 1) & 255

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        os.close(self.master)
        os.close(self.slave)


def wait_for(predicate, seconds=12):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(0.05)
    raise AssertionError("Timed out waiting for integration condition")


def test_socket_to_controller_to_serial(tmp_path):
    web = Path(
        os.environ.get(
            "MURIN_WEB_ROOT", Path(__file__).resolve().parents[4] / "murin-web"
        )
    )
    server = subprocess.Popen(
        [
            "node",
            "-e",
            """
const http = require('http');
const {Server} = require('socket.io');
const {RobotSocket} = require('./server/src/utils/robot_socket');
const server = http.createServer();
const io = new Server(server);
new RobotSocket(io.of('/robot'));
server.listen(0, '127.0.0.1', () => console.log(server.address().port));
""",
        ],
        cwd=web,
        stdout=subprocess.PIPE,
        text=True,
    )
    firmware = Firmware()
    launch = None
    client = socketio.Client(reconnection=False)
    messages, imus, statuses = [], [], []
    client.on("robot_msg", messages.append, namespace="/robot")
    client.on("imu", imus.append, namespace="/robot")
    client.on("imu_status", statuses.append, namespace="/robot")
    rclpy.init(domain_id=91)
    node = rclpy.create_node("integration_observer")
    twists, enabled, joints = [], [], []
    competing = node.create_publisher(TwistStamped, "/cmd_vel", 1)
    node.create_subscription(TwistStamped, "/cmd_vel", twists.append, 10)
    node.create_subscription(Bool, "/murin/drive_enabled", enabled.append, 10)
    node.create_subscription(JointState, "/joint_states", joints.append, 10)
    spin_stop = threading.Event()

    def spin():
        while not spin_stop.is_set():
            rclpy.spin_once(node, timeout_sec=0.05)

    spin_thread = threading.Thread(target=spin)
    spin_thread.start()
    log = (tmp_path / "launch.log").open("w")
    try:
        assert select.select([server.stdout], [], [], 5)[0], (
            "Test server failed to start"
        )
        port = int(server.stdout.readline())
        url = f"http://127.0.0.1:{port}"
        client.connect(url, namespaces=["/robot"], transports=["polling"])
        env = dict(os.environ, ROS_DOMAIN_ID="91")
        launch = subprocess.Popen(
            [
                "ros2",
                "launch",
                "murin_control",
                "murin.launch.py",
                "gui:=false",
                "transport:=serial",
                f"serial_port:={firmware.port}",
                "enable_socket_bridge:=true",
                f"server_url:={url}",
            ],
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        wait_for(lambda: any(s["connected"] for s in statuses))
        wait_for(lambda: imus and any(m["msgType"] == 3 for m in messages))
        assert imus[-1]["quaternion"] == [1, 0, 0, 0]
        assert (
            next(m for m in messages if m["msgType"] == 3)["telemetry"]["voltage"] == 12
        )
        wait_for(
            lambda: (
                "Configured and activated murin_base_controller"
                in (tmp_path / "launch.log").read_text()
            )
        )
        # Fresh wheel commands pass through the diff-drive controller and become float32 m/s.
        for _ in range(100):
            client.emit("cmd_vel", {"left": 0.1, "right": 0.1}, namespace="/robot")
            time.sleep(0.05)
            if any(abs(left - 0.1) < 0.001 for _, (left, right) in firmware.commands):
                break
        assert any(
            abs(left - 0.1) < 0.001 and abs(right - 0.1) < 0.001
            for _, (left, right) in firmware.commands
        )
        wait_for(lambda: joints and any(abs(x) > 0 for x in joints[-1].position))
        wait_for(
            lambda: any(
                abs(m.get("driveState", {}).get("leftVelocity", 0) - 0.1) < 0.001
                for m in messages
            )
        )
        # Timeout does not keep replaying the last browser command.
        time.sleep(0.8)
        assert firmware.commands[-1][1] == (0, 0)
        client.emit("estop", namespace="/robot")
        time.sleep(0.2)
        stopped_at = time.monotonic()
        for _ in range(10):
            client.emit("cmd_vel", {"left": 0.2, "right": 0.2}, namespace="/robot")
            bypass = TwistStamped()
            bypass.header.stamp = node.get_clock().now().to_msg()
            bypass.twist.linear.x = 0.2
            competing.publish(bypass)
            time.sleep(0.05)
        assert all(
            wheels == (0, 0)
            for stamp, wheels in firmware.commands
            if stamp > stopped_at
        )
        reset = node.create_client(Trigger, "/murin_socket_bridge/reset_estop")
        assert reset.wait_for_service(timeout_sec=3)
        future = reset.call_async(Trigger.Request())
        wait_for(future.done)
        assert future.result().success
        resumed_at = time.monotonic()
        for _ in range(20):
            client.emit("cmd_vel", {"left": 0.1, "right": 0.1}, namespace="/robot")
            time.sleep(0.05)
        assert any(
            stamp > resumed_at and left > 0.05
            for stamp, (left, right) in firmware.commands
        )
        client.disconnect()
        time.sleep(0.7)
        assert firmware.commands[-1][1] == (0, 0)
        client.connect(url, namespaces=["/robot"], transports=["polling"])
        after_disconnect = time.monotonic()
        for _ in range(10):
            client.emit("cmd_vel", {"left": 0.2, "right": 0.2}, namespace="/robot")
            time.sleep(0.05)
        assert all(
            wheels == (0, 0)
            for stamp, wheels in firmware.commands
            if stamp > after_disconnect
        )
        # Loss of drive telemetry stops hardware even while ACKs keep arriving.
        firmware.telemetry = False
        wait_for(lambda: statuses and not statuses[-1]["connected"])
        assert firmware.commands[-1][1] == (0, 0)
    finally:
        spin_stop.set()
        spin_thread.join(timeout=2)
        node.destroy_node()
        rclpy.shutdown()
        client.disconnect()
        if launch is not None:
            launch.send_signal(signal.SIGINT)
            try:
                launch.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(launch.pid, signal.SIGKILL)
                launch.wait()
        server.terminate()
        server.wait(timeout=5)
        firmware.close()
        log.close()
        print((tmp_path / "launch.log").read_text())
