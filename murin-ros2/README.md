# Murin ROS 2

ROS 2 Jazzy control for Murin. The C++ plugin owns the robot serial port; a separate Python ROS node connects to the web server in `ROBOT_TRANSPORT=socket` mode. Direct web serial mode is an alternative and must not run against the same device.

## Setup and build

From `murin-ros2`:

```bash
source scripts/activate.sh
rosdep install --from-paths src/murin_control --ignore-src -r -y
./scripts/setup-venv.sh
source .venv/bin/activate
./scripts/build-all.sh
```

`scripts/setup-venv.sh` creates `.venv` and installs `pyproject.toml` and `requirements.txt`. Activation sources Jazzy, `.venv` when available, and `install/setup.bash` when built. Set `MURIN_ROS_SETUP` or `MURIN_VENV` to use other locations. Scripts resolve the workspace independently of the calling directory. Build uses `/usr/bin/python3` for Jazzy’s system-installed CMake/Python dependencies, even when an isolated virtual environment is active. Build arguments are forwarded to colcon; without arguments, the build selects Murin and its dependencies.

Murin includes its own schematic robot description and RViz configuration; the upstream example packages are not required.

## Run

Start `murin-web` first with `ROBOT_TRANSPORT=socket` and `HTTP_PORT=9091` in its local `server/.env`. Run a simulation:

```bash
./scripts/run-all.sh
```

For the robot:

```bash
./scripts/run-all.sh --hardware --serial-port /dev/murin-cdc \
  --server-url http://localhost:9091 \
  --wheel-radius 0.015 --wheel-separation 0.10
```

**Replace the demo dimensions with measured values in metres.** The baud rate defaults to 2000000. `--server-url` / `MURIN_SERVER_URL` must match the server's actual address and `HTTP_PORT`; localhost means this computer. `MURIN_SERIAL_PORT` can override the conventional device alias.

Use `--dry-run` to print the command, `--check` for preflight only, or `--gui` for RViz. The launcher rejects an existing Murin launch or a busy serial port and holds a workspace lock for its lifetime. It does not kill other processes. Ctrl+C shuts down its own ROS launch cleanly. It does not install dependencies or build automatically.

For a separately managed Python service, use `--external-bridge`, then run:

```bash
ros2 run murin_control robot_socket_bridge --ros-args \
  -p server_url:=http://localhost:9091 -p wheel_separation:=0.10
```

`--external-bridge` retains the C++ watchdog; `--no-bridge` instead selects standalone ROS control without the socket watchdog. The launch helper can check HTTP reachability but cannot determine the remote server's configured transport; source registration verifies the socket contract at runtime.

## Recovery and diagnosis

After an ESP32 reset or serial error, the driver closes its connection and does not reconnect automatically. Stop the old ROS launch, confirm there is only one stack and the port is available, then start once. Do not launch a second copy to recover. A dashboard/server disconnect or estop remains latched until the intentional reset service is called; see the [control package guide](src/murin_control/README.md).

For fresh-data checks, inspect `/murin/hardware_connected`, `/murin/serial_frames`, `/joint_states`, and `/murin_base_controller/odom`. Confirm firmware timestamps advance. The configured controller rate is 50 Hz and bridge/watchdog rate is 20 Hz; sensor/telemetry rates depend on firmware. Current drive feedback represents applied motor values, not encoder measurements. Avoid temporary dashboard socket clients for passive checks: disconnecting them latches estop.

## Test without hardware

```bash
./scripts/test-all.sh
./scripts/test-all.sh --integration -q
```

The integration test needs the adjacent web dependencies (`npm ci --ignore-scripts` in `murin-web`) and Python requirements above. It uses an ephemeral server and PTY, not the running server or robot. See [AGENTS.md](AGENTS.md) for contributor instructions and the [control guide](src/murin_control/README.md) for protocol details.

## Formatting

```bash
./scripts/format-all.sh          # Apply formatting
./scripts/format-all.sh --check  # Check only; return nonzero for differences
```

Requires `clang-format`, `ruff`, and `cmake-format` on PATH. Install the Python tools in your active virtual environment with `python -m pip install -r requirements.txt`. Formatting does not require ROS activation or access to hardware.

The script formats C/C++, Python (including the extensionless bridge executable), and CMake files under `src/murin_control` and `scripts`. It uses the workspace `.clang-format`, based on the upstream ROS demo style. Upstream demos, symlinks, build/install/log directories, virtual environments, and caches are excluded. All required tools are checked before any files are modified. This entry point is Bash-only.
