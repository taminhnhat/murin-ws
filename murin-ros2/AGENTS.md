# Murin ROS 2 instructions

Read `README.md` and `src/murin_control/README.md` before changing launch, transport, or runtime behavior. This is a ROS 2 Jazzy workspace using a merged colcon install. Treat `src/ros2_control_demos` as the upstream reference; make Murin changes in `src/murin_control`.

## Components

- `hardware/murin_system.cpp` owns serial communication and ros2_control lifecycle, command limits, feedback and watchdog handling.
- `hardware/include/murin_control/serial_link.hpp` implements framing, CRC, payload stuffing, nonblocking serial access and host sequence wrap.
- `murin_control_bridge/bridge.py` is a separate Python ROS service. It handles Socket.IO registration/reconnection and dashboard command conversion. Keep network I/O outside the C++ controller loop.
- `murin_control_bridge/protocol.py` decodes battery (0x03/22 bytes), IMU (0x04/62 bytes), and drive state (0x05/20 bytes). Firmware timestamps are not assumed to share the ROS clock. IMU quaternion order is w,x,y,z.
- Drive feedback uses firmware left/right m/s values, converted to rad/s and integrated using firmware timestamps. Current firmware reports applied motor values, not encoders. Do not claim closed-loop measured wheel speed.
- Preserve ±0.5 m/s motor bounds, command/watchdog freshness, latched stop, serial fault handling, timestamp wrap and zero commands on lifecycle shutdown. No unsupported SET_TIME message should be sent.

## Scripts and runtime

- `./scripts/setup-venv.sh` creates the repository `.venv` from `requirements.txt`. `source scripts/activate.sh` loads Jazzy, the `.venv`, and the built workspace. `MURIN_ROS_SETUP` and `MURIN_VENV` override the environment locations; scripts must not embed a temporary venv path.
- `scripts/build-all.sh` builds Murin and its workspace dependencies with `--merge-install`. It does not run hardware or install dependencies.
- `scripts/run-all.sh` defaults to simulation plus the Python bridge. `--hardware --serial-port /dev/murin-cdc` selects a physical device. Use `--server-url` or `MURIN_SERVER_URL` to override port 9091.
- `--dry-run` only prints the command. `--check` performs preflight without starting ROS or opening serial devices. Preserve the duplicate-launch lock and ownership checks.
- Use `--external-bridge` when running the Python service separately; it retains the C++ watchdog. `--no-bridge` is standalone ROS control without the socket watchdog. Do not confuse these modes.
- Do not run a second launch to diagnose an existing one. Inspect process IDs, controller state, `/murin/hardware_connected`, `/murin/serial_frames`, `/joint_states`, and odometry. Repeated inactive-subscriber messages are not healthy control-loop operation.
- ESP32 reset requires a clean ROS restart after a serial error. Do not add automatic hardware reconnection or automatic estop reset without an explicit behavior change request.

## Checks

```bash
python3 -m unittest discover -s scripts/tests -v
./scripts/build-all.sh
./scripts/test-all.sh
# Requires Python Socket.IO client and adjacent web checkout dependencies:
./scripts/test-all.sh --integration -q
```

Launch tests use isolated ROS domains. The integration test uses an ephemeral server and PTY emulator; it must never load server `.env` or select real serial hardware. Keep the PTY raw before sending telemetry to avoid echo corrupting its parser. Test async telemetry with bounded waits rather than immediate assertions.

For script changes, run Bash syntax checks and dry-run/check paths. Do not modify the user's running ROS stack for a script check. Report build/test results and distinguish simulated tests from real-device observations.

ROS script entry points are Bash-only (`.sh`); no `.ps1` counterparts are required.

## Formatting

Use `scripts/format-all.sh` to format or `scripts/format-all.sh --check` to validate C/C++, Python, and CMake under `src/murin_control` and `scripts`. The helper must exclude upstream demos, generated files, and symlinks. Keep `.clang-format` consistent with the ROS demo style. Dependencies are `clang-format`, `ruff`, and `cmake-format`; report missing tools rather than claiming a successful format check. Do not add a PowerShell wrapper.
