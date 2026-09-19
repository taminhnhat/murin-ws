# Murin control

ROS 2 Jazzy differential-drive control, adapted from `ros2_control_demos/example_2` (Apache-2.0).

## Server modes

| Server setting | Robot serial owner | Command path |
| --- | --- | --- |
| `ROBOT_TRANSPORT=serial` | Web server `robot_link.js` | Dashboard → web server → firmware |
| `ROBOT_TRANSPORT=socket` | C++ `murin_control/MurinSystemHardware` | Dashboard → `robot_socket.js` → Python ROS bridge → diff-drive controller → C++ hardware → firmware |

The separate `robot_socket_bridge` ROS node is a Socket.IO client of the server's `/robot` namespace. C++ has no Socket.IO dependency. Serial telemetry travels through `/murin/serial_frames` to Python, which emits the existing `robot_msg` and `imu` dashboard events. Hardware status travels through `/murin/hardware_connected` to `robot_source_status`.

## Build and run

Prefer the workspace [startup scripts](../../README.md): `scripts/build-all.sh`, `scripts/run-all.sh`, and `scripts/test-all.sh`. The launcher prevents duplicate Murin stacks. The raw launch commands below are useful when configuring a service manager; manage single-instance ownership there.

From `murin-ros2`:

```bash
source /opt/ros/jazzy/setup.bash
rosdep install --from-paths src/murin_control --ignore-src -r -y
colcon build --merge-install --packages-up-to murin_control
source install/setup.bash
```

The Python bridge needs python-socketio 5.x (compatible with the server's Socket.IO 4.x). If it is not provided by the OS, install it in a virtual environment that can access ROS:

```bash
/usr/bin/python3 -m venv --system-site-packages .venv
source .venv/bin/activate
python -m pip install -r src/murin_control/requirements.txt
```

Set `ROBOT_TRANSPORT=socket` in `murin-web/server/.env` and start the web server. This mode opens no server serial ports, including the console. Launch ROS with an explicitly selected robot port:

```bash
ros2 launch murin_control murin.launch.py \
  gui:=false transport:=serial serial_port:=/dev/murin-cdc \
  baud_rate:=2000000 enable_socket_bridge:=true \
  server_url:=http://localhost:9091 \
  wheel_radius:=0.015 wheel_separation:=0.10
```

**The dimensions above are demo placeholders. Set the measured wheel radius and separation in metres.** These launch values feed the hardware conversion, controller, and bridge. The package includes its own schematic chassis and RViz configuration; no demo packages are required. Displayed wheel radius and separation follow the launch values. Chassis dimensions are placeholders. Do not run direct server serial mode and ROS serial mode against the same robot port. The Linux serial backend claims exclusive access; supported baud rates are 115200, 921600, 1000000, and 2000000.

Without hardware, use `transport:=simulation` (the default). `enable_socket_bridge:=true` can also exercise the dashboard against the simulated C++ plugin. `use_mock_hardware:=true` selects the upstream GenericSystem for standalone demo use; it does not provide the Murin bridge status or watchdog topics.

To run the Python node separately (for example under a service manager), launch the control stack with `enable_socket_bridge:=false require_bridge:=true`, then run:

```bash
ros2 run murin_control robot_socket_bridge --ros-args \
  -p server_url:=http://localhost:9091 -p wheel_separation:=0.10
```

Use the same wheel separation as the controller. Run only one bridge per robot. The combined launch with `enable_socket_bridge:=true` starts both processes for you.

## Commands and stops

Dashboard `cmd_vel` is `{left, right}` in m/s, each finite and within ±0.5. The Python bridge publishes `geometry_msgs/TwistStamped` on `/cmd_vel`: linear X is `(left+right)/2`; angular Z is `(right-left)/wheel_separation`. The diff-drive controller commands rad/s; C++ multiplies by wheel radius before encoding two little-endian float32 values as `CMD_MOTOR` (0x01). If necessary, it scales both wheels together to keep speeds within ±0.5 m/s.

The bridge grants motion only while dashboard commands are less than 0.5 seconds old. It publishes `/murin/drive_enabled` at 20 Hz; C++ stops if permission is false or stale for 0.5 seconds. With the bridge disabled, the ROS controller's own `/cmd_vel` timeout applies. `estop` and a dashboard/server disconnect latch a bridge stop. Reconnecting does not resume motion. Reset deliberately, then send a fresh command:

```bash
ros2 service call /murin_socket_bridge/reset_estop std_srvs/srv/Trigger '{}'
```

Serial write/read errors, firmware NACKs, or missing drive-state telemetry for one second stop and close the hardware connection. Deactivation, cleanup, shutdown, and errors send zero motor commands. After a serial fault, fix the connection and restart the control stack; serial reconnection is deliberately not automatic. The firmware's own motor-command watchdog remains the fallback if the host process or cable disappears.

Socket source registration must be acknowledged before the bridge is ready. The server accepts one robot source, sends initial status to new dashboards, and relays telemetry only from the registered source. `goal` is received and logged as unsupported; navigation is not implemented here.

## Firmware feedback

Framing matches `murin-firmware/protocol.md`: 0xAA start, payload byte stuffing, little-endian stuffed length, CRC-16/CCITT-FALSE, and a wrapping shared host sequence.

- `0x03`: 22-byte battery state, forwarded in `robot_msg.telemetry`.
- `0x04`: 62-byte IMU state, forwarded as `imu`; quaternion order is **w,x,y,z**, and the int64 firmware timestamp is preserved as a decimal string.
- `0x05`: 20-byte drive state, forwarded as `robot_msg.driveState`. Wheel velocities in m/s become ROS joint velocities in rad/s. Positions integrate feedback using the firmware's uint32 millisecond timestamp, including wraparound.

The current firmware uses applied motor values for drive telemetry, not encoder measurements. Odometry therefore reflects reported applied commands. The ROS implementation uses left/right feedback and configured geometry rather than the firmware's current angular-velocity placeholder. Drive telemetry (mask bit 3) must remain enabled with an interval below one second. No unsupported SET_TIME command is sent.

## Verification

```bash
colcon test --merge-install --packages-select murin_control
colcon test-result --test-result-base build/murin_control --verbose
```

The cross-repository test also needs `npm ci --ignore-scripts` in `murin-web` and the Python requirements above:

```bash
python -m pytest src/murin_control/test/test_socket_integration.py -q
```

It launches an ephemeral Socket.IO server and pseudo-terminal firmware emulator. It does not use `.env`, real serial ports, or robot hardware. Set `MURIN_WEB_ROOT` if the web checkout is not alongside `murin-ros2`. It tests dashboard commands through the actual controller/plugin, telemetry forwarding, command timeout, a latched stop, and feedback loss.
