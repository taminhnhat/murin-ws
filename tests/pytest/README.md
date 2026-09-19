# Pytest tests

This directory contains pytest-based tests for the ESP32 framed-link, ROS2
protocol, and UART shell. The tests require a running firmware image and use
serial ports configured in `test_config.yaml`.

## Test files

- `test_protocol.py` — ROS2 heartbeat, motor, servo, configuration, battery/IMU telemetry, and invalid-command tests.
- `test_console.py` — UART shell commands, diagnostics, and monitor start/stop tests.
- `test_perf.py` — heartbeat ACK latency and throughput test.
- `test_stress.py` — sustained heartbeat test with a live progress bar.
- `conftest.py` — shared serial fixtures and configuration loading.
- `test_config.yaml` — local serial ports and test thresholds.

## Prerequisites

From the repository root, install the Python dependencies and flash the
ESP32-S3 firmware:

```powershell
python -m pip install -r requirements.txt
```

Connect the board over USB before running hardware-backed tests.

## Configure serial ports

Edit [`test_config.yaml`](test_config.yaml):

```yaml
cdc_port: "COM12"
console_port: "COM10"
console_baudrate: 115200
baudrate: 2000000
```

`cdc_port` is used by the ROS2 protocol, performance, and stress tests.
`console_port` is used by the UART shell tests. A missing port causes the
corresponding hardware tests to be skipped.

## Run tests

Run all pytest tests from the repository root:

```powershell
python -m pytest tests/pytest -v
```

Run an individual test file:

```powershell
python -m pytest tests/pytest/test_protocol.py -v
python -m pytest tests/pytest/test_console.py -v
python -m pytest tests/pytest/test_perf.py -v -s
python -m pytest tests/pytest/test_stress.py -v -s
```

Run one test by name:

```powershell
python -m pytest tests/pytest/test_protocol.py::test_ros2_heartbeat_command -v -s
```

The repository-level [`scripts/test-all.ps1`](../../scripts/test-all.ps1)
also runs pytest and the GoogleTest suite together.

## Performance settings

`test_perf.py` reads these optional keys from `test_config.yaml`:

```yaml
perf_iterations: 200
perf_warmup_iterations: 10
perf_ack_timeout: 0.5
perf_ack_retries: 2
perf_max_avg_ack_ms: 50
perf_max_p95_ack_ms: 200
```

Increase the timeout or latency thresholds if the board is running a debug
build or the host is under heavy load.

## Stress settings

`test_stress.py` runs for 30 seconds by default. Configure it with:

```yaml
stress_duration_s: 30
stress_ack_timeout: 0.5
stress_ack_retries: 2
```

When the stress-specific timeout and retry values are omitted, the test falls
back to the corresponding performance settings.

## Troubleshooting

If pytest cannot open a serial port, close ESP-IDF monitor, Serial Studio, or
any other program using the same port.

If ACK tests time out, confirm that the firmware implements the matching
protocol and that the configured baud rate matches the host configuration.

If tests are skipped, check that the required port is present in
`test_config.yaml`.

If imports fail, reinstall the dependencies:

```powershell
python -m pip install -r requirements.txt
```

If pytest warns that it cannot write `.pytest_cache`, fix the cache directory
permissions or remove the stale cache. The warning does not mean the tests
failed.
