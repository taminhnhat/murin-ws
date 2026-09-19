#!/usr/bin/env python3
"""
test_stress.py - Sustained ESP32 USB protocol heartbeat stress test.

The test runs for 30 seconds by default.  The duration and serial retry
settings can be overridden in test_config.yaml with:

    stress_duration_s: 30
    stress_ack_timeout: 0.5
    stress_ack_retries: 2
"""

import sys
import time
from pathlib import Path

# Add the pytest and utils directories to the import path.  The pytest
# directory is needed when this module is imported outside of test collection.
TEST_DIR = Path(__file__).parent
sys.path.insert(0, str(TEST_DIR))
sys.path.insert(0, str(TEST_DIR.parent.parent / "utils"))

from test_perf import heartbeat_with_retry


DEFAULT_DURATION_S = 30.0


def _stress_duration(test_config):
    """Read the duration while accepting the longer historical key name."""
    if "stress_duration_s" in test_config:
        return float(test_config["stress_duration_s"])
    if "stress_duration_seconds" in test_config:
        return float(test_config["stress_duration_seconds"])
    return DEFAULT_DURATION_S


def test_heartbeat_stress(serial_agent, test_config):
    """Exercise heartbeat handling continuously and detect intermittent loss."""
    duration_s = _stress_duration(test_config)
    ack_timeout = float(
        test_config.get("stress_ack_timeout", test_config.get("perf_ack_timeout", 0.5))
    )
    ack_retries = int(
        test_config.get("stress_ack_retries", test_config.get("perf_ack_retries", 2))
    )

    assert duration_s > 0
    assert ack_timeout > 0
    assert ack_retries >= 0

    serial_agent.drain_rx()
    deadline = time.monotonic() + duration_s
    completed = 0
    retry_count = 0
    error_counts = {"ack_timeout": 0, "write_timeout": 0, "serial_error": 0}
    measurement_start = time.perf_counter()

    while time.monotonic() < deadline:
        retries_used, success, error = heartbeat_with_retry(
            serial_agent, ack_timeout, ack_retries
        )
        retry_count += retries_used
        if success:
            completed += 1
        else:
            error_counts[error] += 1

    measurement_duration_s = time.perf_counter() - measurement_start
    message_rate = completed / measurement_duration_s if measurement_duration_s else 0.0
    print(
        f"[STRESS REPORT]\n"
        f"- duration={measurement_duration_s:.3f}s target={duration_s:.3f}s\n"
        f"- heartbeats={completed} rate={message_rate:.1f}msg/s retries={retry_count}\n"
        f"- ack_timeouts={error_counts['ack_timeout']}\n"
        f"- write_timeouts={error_counts['write_timeout']}\n"
        f"- serial_errors={error_counts['serial_error']}"
    )

    assert completed > 0, "stress test received no heartbeat ACKs"
    assert error_counts == {
        "ack_timeout": 0,
        "write_timeout": 0,
        "serial_error": 0,
    }, f"serial errors during stress test: {error_counts}"
