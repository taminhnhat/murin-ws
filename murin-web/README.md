# Murin Web Server

Web dashboard and serial gateway for the Murin robot.

## Requirements

- Node.js and npm
- Python 3
- Access to the robot serial devices

Install dependencies from the `murin-web` directory:

```bash
npm install
```

## Start

Ubuntu:

```bash
./scripts/start-server.sh
```

Windows PowerShell:

```powershell
.\scripts\start-server.ps1
```

On first run, the launcher creates `server/.env` from the template. In serial mode, set `USB_PORT` and `CONSOLE_PORT` before starting. In socket mode, neither port is required. The file is local and ignored by Git.

Validate configuration without starting the server:

```bash
./scripts/start-server.sh --check
```

The default HTTP port is **9091**. Set `HTTP_PORT` to override it and use the same port in the ROS bridge `server_url`.

Starting with `npm start` performs the same port check, but does not create or
edit `.env`.

Ubuntu normally uses:

```dotenv
HTTP_PORT=9091
ROBOT_TRANSPORT=serial
USB_PORT=/dev/murin-cdc
USB_BAUDRATE=2000000
CONSOLE_PORT=/dev/murin-console
CONSOLE_BAUDRATE=115200
```

Windows uses the matching `COM` port names. Restart the server after changing
the configuration. The CDC connection starts with the server; open the Terminal
page and press **A** to connect the console.

### Socket.IO mode

To receive robot data from another Socket.IO client, set:

```dotenv
ROBOT_TRANSPORT=socket
```

In socket mode, serial ports are optional and the server does not open them. A
robot source connects to the `/robot` namespace and emits
`robot_source_register` first. It can then publish `imu`, `robot_msg`, and
`robot_source_status`. The server forwards `cmd_vel`, `goal`, and `estop` from
the dashboard to the registered robot source. Only one source may register at a time.
See [the transport contract](server/README.md) and the
[ROS control setup](../murin-ros2/src/murin_control/README.md) for the Python bridge and C++ hardware configuration.

## Development

```bash
npm test
node --test server/test/robot_socket.test.js
python3 -m unittest discover -s scripts/tests -v
npm run format
npm run format:check
```

The formatting commands use Python and Prettier. You can also run the platform
wrappers directly:

```bash
./scripts/format-all.sh --check
```

```powershell
.\scripts\format-all.ps1 --check
```

## Shell and PowerShell scripts

Run the same workflow on Linux/macOS with `.sh`, or Windows with `.ps1`. Both wrappers use the same Python implementation, forward arguments, and preserve exit codes. Paths resolve from the script location, so callers need not be in the repository directory.

| Workflow | Bash | PowerShell |
| --- | --- | --- |
| Check formatting | `./scripts/format-all.sh --check` | `.\scripts\format-all.ps1 --check` |
| Apply formatting | `./scripts/format-all.sh` | `.\scripts\format-all.ps1` |
| Validate configuration | `./scripts/start-server.sh --check` | `.\scripts\start-server.ps1 --check` |
| Start server | `./scripts/start-server.sh` | `.\scripts\start-server.ps1` |
| Test without hardware | `./scripts/test-all.sh` | `.\scripts\test-all.ps1` |

The test runner checks JavaScript syntax, Socket.IO behavior, Python launcher tests, and formatting. It does not start the configured server or open serial devices.
