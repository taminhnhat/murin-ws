# Murin Firmware

Firmware and host-side tools for the Murin ESP32-S3 control system.

## Table of Contents

- [1. Prerequisites](#1-prerequisites)
- [2. Build and Run](#2-build-and-run)
- [3. Tools](#3-tools)
- [4. Testing](#4-testing)
  - [4.1 Run all tests](#41-run-all-tests)
  - [4.2 Test Suite Details](#42-test-suite-details)
- [5. Code Quality](#5-code-quality)
  - [5.1 Format code](#51-format-code)
  - [5.2 Static analysis](#52-static-analysis)
- [6. Todo](#6-todo)

## 1. Prerequisites

Install the Python and Node.js dependencies from the repository root:

```powershell
python -m pip install -r requirements.txt
npm install
```

## 2. Build and Flash

Activate the ESP-IDF environment in PowerShell before using `idf.py`:

```powershell
C:\esp\v6.0\esp-idf\export.ps1
```

If ESP-IDF is installed in a different location, update the path accordingly.

Build the firmware with:

```powershell
idf.py build
```

Flash the firmware and open the monitor with:

```powershell
idf.py flash monitor -p COM10
```

Connect the board over USB before running hardware-backed tests or monitor
tools.

## 3. Tools

The parser tools, serial configuration, and command examples are documented in
[`tools/README.md`](tools/README.md).

## 4. Testing

### 4.1 Run all tests

Run pytest, GoogleTest, and LLVM coverage analysis from the repository root:

```powershell
.\scripts\test-all.ps1
```

The coverage summary is printed after the tests. The annotated HTML report is
written to `tests/gtest/build-coverage/html/index.html`.

Pass additional arguments to pytest:

```powershell
.\scripts\test-all.ps1 -v
```

### 4.2 Test Suite Details

Detailed pytest instructions, configuration, and troubleshooting are
documented in [`tests/pytest/README.md`](tests/pytest/README.md).

The GoogleTest build and direct commands are documented in
[`tests/gtest/README.md`](tests/gtest/README.md).

## 5. Code Quality

### 5.1 Format code

Run all configured formatters:

```powershell
.\scripts\format-all.ps1
```

The script formats:

- C/C++ files under `main` and `tests` with `clang-format`
- Python files under `tests`, `tools`, and `utils` with Ruff
- CMake files with `cmake-format`

Install the Python formatters with:

```powershell
python -m pip install ruff cmakelang
```

The LLVM installation provides `clang-format`.

### 5.2 Static analysis

The repository includes [`.clang-tidy`](.clang-tidy). After generating the
compile database, run clang-tidy with:

```powershell
idf.py reconfigure
python "C:\Program Files\LLVM\bin\run-clang-tidy" -p build
```

## 6. Todo
| TODO | Priority | Direction | Message | Purpose | Response |
|---|---|---|---|---|---|
| [x] | P0 | Host → Robot | `HEARTBEAT` | Maintain host connection and trigger safe state on timeout | ACK |
| [x] | P0 | Host → Robot | `CMD_MOTOR` | Set drive/motor command | ACK/NACK |
| [x] | P1 | Host → Robot | `CMD_SERVO` | Set servo position/command | ACK/NACK |
| [x] | P0 | Robot → Host | `TELEMETRY_BATTERY` | Report battery voltage, current, energy, etc. | None |
| [x] | P0 | Robot → Host | `TELEMETRY_IMU` | Report IMU measurements | None |
| [x] | P0 | Robot → Host | `TELEMETRY_DRIVE_STATE` | Report actual drive/motor state | None |
| [x] | P0 | Robot → Host | `ACK` | Confirm successful command processing | None |
| [x] | P0 | Robot → Host | `NACK` | Report command rejection/error | None |
| [ ] | P0 | Host → Robot | `GET_PROTOCOL_INFO` | Get wire protocol version, firmware version and device identity | `PROTOCOL_INFO` |
| [ ] | P0 | Robot → Host | `PROTOCOL_INFO` | Return protocol, firmware and device information | None |
| [ ] | P0 | Host → Robot | `GET_CAPABILITIES` | Discover supported robot/device features | `CAPABILITIES` |
| [ ] | P0 | Robot → Host | `CAPABILITIES` | Return supported capabilities/features | None |
| [ ] | P0 | Host → Robot | `GET_STATE` | Get current controller/system state after connection | `STATE` |
| [ ] | P0 | Robot → Host | `STATE` | Return current state, safety state and relevant flags | None |
| [x] | P0 | Host → Robot | `SET_TIME` | Synchronize ESP32 UTC using host Unix timestamp | ACK/NACK |
| [ ] | P0 | Host → Robot | `SET_TELEMETRY` | Enable/disable a telemetry stream and configure its reporting rate | ACK/NACK |
| [ ] | P0 | Host → Robot | `ESTOP` | Immediately enter emergency/safe stop state | ACK/NACK |
| [ ] | P0 | Host → Robot | `CLEAR_ESTOP` / `ARM` | Explicitly recover from ESTOP and allow drive operation | ACK/NACK |
| [ ] | P1 | Host → Robot | `GET_CONFIG` | Read a configuration value by configuration ID/key | `CONFIG_VALUE` |
| [ ] | P1 | Robot → Host | `CONFIG_VALUE` | Return requested configuration value | None |
| [ ] | P1 | Host → Robot | `SET_CONFIG` | Change a runtime configuration value | ACK/NACK |
| [ ] | P1 | Host → Robot | `SAVE_CONFIG` | Persist runtime configuration to NVS | ACK/NACK |
| [ ] | P1 | Host → Robot | `RESET_CONFIG` | Restore configuration defaults | ACK/NACK |
| [ ] | P1 | Host → Robot | `GET_TIME_STATUS` | Check whether UTC has been synchronized and obtain synchronization status | `TIME_STATUS` |
| [ ] | P1 | Robot → Host | `TIME_STATUS` | Return UTC synchronization state | None |
| [ ] | P1 | Host → Robot | `REBOOT` | Perform controlled ESP32 reboot | ACK/NACK |
| [ ] | P2 | Host → Robot | `GET_DIAGNOSTICS` | Read faults, reset reason, counters and diagnostic information | `DIAGNOSTICS` |
| [ ] | P2 | Robot → Host | `DIAGNOSTICS` | Return diagnostic information | None |
| [ ] | P2 | Host → Robot | `CLEAR_DIAGNOSTICS` | Clear stored diagnostic/fault information | ACK/NACK |
| [ ] | P2 | Host → Robot | `PING` | Test protocol link and measure round-trip behavior | `PONG` |
| [ ] | P2 | Robot → Host | `PONG` | Respond to `PING` | None |
| [ ] | P2 | Robot → Host | `TIME_SYNC_REQUIRED` | Notify host that valid absolute UTC time is unavailable | None |
| [ ] | P1 | Internal | `BNO085_RECOVERY` | Detect repeated BNO085 read timeouts, reset the sensor, and re-enable configured reports | None |
