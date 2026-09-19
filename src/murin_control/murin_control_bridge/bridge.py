"""Socket.IO runs in a worker; all ROS state and command handling runs in the executor."""

import queue
import threading
import time
import math
import struct

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, DurabilityPolicy
from geometry_msgs.msg import TwistStamped
from std_msgs.msg import Bool, UInt8MultiArray
from std_srvs.srv import Trigger
import socketio

from murin_control_bridge.protocol import decode_frame, wheel_twist


class RobotBridge(Node):
    def __init__(self):
        super().__init__("murin_socket_bridge")
        self.url = self.declare_parameter("server_url", "http://localhost:9091").value
        self.separation = self.declare_parameter("wheel_separation", 0.1).value
        if not math.isfinite(self.separation) or self.separation <= 0:
            raise ValueError("wheel_separation must be positive")
        self.incoming = queue.Queue(maxsize=100)
        self.outgoing = queue.Queue(maxsize=200)
        self.halt = threading.Event()
        self.network_stop = threading.Event()
        self.network_stop.set()
        self.connected = False
        self.hardware = False
        self.latched = False
        self.last_command = 0.0
        self.command_epoch = time.monotonic()
        self.command = (0.0, 0.0)
        self.cmd_pub = self.create_publisher(TwistStamped, "/cmd_vel", 1)
        self.enable_pub = self.create_publisher(Bool, "/murin/drive_enabled", 1)
        self.create_subscription(
            Bool,
            "/murin/hardware_connected",
            self.hardware_status,
            QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL),
        )
        self.create_subscription(
            UInt8MultiArray, "/murin/serial_frames", self.telemetry, 10
        )
        self.create_service(Trigger, "~/reset_estop", self.reset_estop)
        self.create_timer(0.05, self.tick)
        self.worker = threading.Thread(target=self.network, daemon=True)
        self.worker.start()

    def enqueue(self, kind, data=None):
        try:
            self.incoming.put_nowait((kind, data, time.monotonic()))
        except queue.Full:
            self.network_stop.set()

    def emit(self, kind, data):
        try:
            self.outgoing.put_nowait((kind, data))
        except queue.Full:
            pass  # Telemetry is expendable; never block the control executor.

    def network(self):
        while not self.halt.is_set():
            client = socketio.Client(reconnection=False, request_timeout=2)
            client.on(
                "cmd_vel",
                lambda data: self.enqueue("cmd_vel", data),
                namespace="/robot",
            )
            client.on(
                "estop", lambda *args: self.network_stop.set(), namespace="/robot"
            )
            client.on(
                "disconnect", lambda *args: self.network_stop.set(), namespace="/robot"
            )
            client.on(
                "goal", lambda data: self.enqueue("goal", data), namespace="/robot"
            )
            try:
                client.connect(
                    self.url,
                    namespaces=["/robot"],
                    transports=["polling"],
                    wait_timeout=3,
                )
                answer = client.call(
                    "robot_source_register",
                    {"name": "Murin ROS 2"},
                    namespace="/robot",
                    timeout=3,
                )
                if not isinstance(answer, dict) or answer.get("ok") is not True:
                    raise RuntimeError("Server rejected robot source registration")
                # Clear old telemetry before a new connection; never replay motor commands.
                while not self.outgoing.empty():
                    try:
                        self.outgoing.get_nowait()
                    except queue.Empty:
                        break
                self.enqueue("connected")
                while client.connected and not self.halt.is_set():
                    try:
                        kind, data = self.outgoing.get(timeout=0.1)
                        client.emit(kind, data, namespace="/robot")
                    except queue.Empty:
                        continue
            except Exception as error:
                self.get_logger().warning(f"Socket connection: {error}")
            finally:
                self.network_stop.set()
                self.enqueue("disconnected")
                client.disconnect()
            self.halt.wait(2)

    def hardware_status(self, msg):
        self.hardware = msg.data
        if not self.hardware:
            self.command = (0.0, 0.0)
            self.last_command = 0.0
        self.publish_status()

    def publish_status(self):
        self.emit(
            "robot_source_status",
            dict(
                connected=self.hardware,
                message=(
                    "Murin ROS hardware active"
                    if self.hardware
                    else "Murin ROS hardware inactive"
                )
                + ("; stop latched, reset required" if self.latched else ""),
            ),
        )

    def telemetry(self, msg):
        try:
            message, imu = decode_frame(msg.data)
        except (ValueError, struct.error):
            self.get_logger().warning("Discarded malformed firmware telemetry")
            return
        self.emit("robot_msg", message)
        if imu is not None:
            self.emit("imu", imu)

    def reset_estop(self, request, response):
        response.success = self.connected and self.hardware
        response.message = (
            "Stop reset; send a fresh command"
            if response.success
            else "Server and hardware must be connected"
        )
        if response.success:
            self.latched = False
            self.network_stop.clear()
            self.command = (0.0, 0.0)
            self.last_command = 0.0
            self.command_epoch = time.monotonic()
            self.publish_status()
        return response

    def tick(self):
        for _ in range(100):
            try:
                kind, data, stamp = self.incoming.get_nowait()
            except queue.Empty:
                break
            if kind == "connected":
                self.connected = True
                # Initial connection can start; later disconnects remain latched.
                if not self.latched:
                    self.network_stop.clear()
                self.publish_status()
            elif kind == "disconnected":
                if self.connected:
                    self.latched = True
                self.connected = False
                self.command = (0.0, 0.0)
                self.last_command = 0.0
            elif (
                kind == "cmd_vel"
                and stamp > self.command_epoch
                and self.connected
                and self.hardware
                and not self.latched
                and not self.network_stop.is_set()
            ):
                try:
                    self.command = wheel_twist(data, self.separation)
                    self.last_command = stamp
                except ValueError as error:
                    self.get_logger().warning(str(error))
            elif kind == "goal":
                self.get_logger().warning(
                    "Navigation goal ignored: no navigation stack configured"
                )
        if self.connected and self.network_stop.is_set() and not self.latched:
            self.latched = True
            self.publish_status()
        enabled = (
            self.connected
            and self.hardware
            and not self.latched
            and not self.network_stop.is_set()
        )
        # Only a fresh dashboard command grants motion, including from competing ROS publishers.
        fresh = time.monotonic() - self.last_command < 0.5
        self.enable_pub.publish(Bool(data=enabled and fresh))
        cmd = TwistStamped()
        cmd.header.stamp = self.get_clock().now().to_msg()
        if enabled and fresh:
            cmd.twist.linear.x, cmd.twist.angular.z = self.command
        self.cmd_pub.publish(cmd)

    def close(self):
        self.halt.set()
        self.enable_pub.publish(Bool(data=False))
        self.cmd_pub.publish(TwistStamped())
        self.worker.join(timeout=6)


def main():
    rclpy.init()
    node = RobotBridge()
    try:
        rclpy.spin(node)
    except (KeyboardInterrupt, rclpy.executors.ExternalShutdownException):
        pass
    finally:
        if rclpy.ok():
            node.close()
        else:
            node.halt.set()
            node.worker.join(timeout=6)
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
