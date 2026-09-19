#!/usr/bin/env python3
"""
test_perf.py - Basic ESP32 USB protocol latency/performance smoke test.

Optional keys in utils/test_config.yaml:
    perf_iterations: 50
    perf_warmup_iterations: 5
    perf_ack_timeout: 0.5
    perf_ack_retries: 2
    perf_max_avg_ack_ms: 50
    perf_max_p95_ack_ms: 200
"""

import sys
from pathlib import Path

# Add utils directory to path
sys.path.insert(0, str(Path(__file__).parent.parent.parent / "utils"))

import statistics
import time

import serial

from protocol_common import MSG_HEARTBEAT


def percentile(values, percent):
    if not values:
        return 0.0
    ordered = sorted(values)
    index = round((len(ordered) - 1) * percent / 100)
    return ordered[index]


def heartbeat_with_retry(serial_agent, timeout, retries):
    """Send a heartbeat and retry transient serial/ACK timeouts."""
    write_error = None
    write_error_type = None
    for attempt in range(retries + 1):
        try:
            seq = serial_agent.send_frame(MSG_HEARTBEAT, b"")
            if serial_agent.wait_for_ack(seq, timeout=timeout):
                return attempt, True, None
        except serial.SerialTimeoutException as exc:
            write_error = exc
            write_error_type = "write_timeout"
        except serial.SerialException as exc:
            write_error = exc
            write_error_type = "serial_error"

    if write_error is not None:
        return retries, False, write_error_type
    return retries, False, "ack_timeout"


def test_heartbeat_ack_latency(serial_agent, test_config):
    iterations = int(test_config.get("perf_iterations", 50))
    warmup_iterations = int(test_config.get("perf_warmup_iterations", 5))
    ack_timeout = float(test_config.get("perf_ack_timeout", 0.5))
    ack_retries = int(test_config.get("perf_ack_retries", 2))
    max_avg_ms = float(test_config.get("perf_max_avg_ack_ms", 50))
    max_p95_ms = float(test_config.get("perf_max_p95_ack_ms", 200))

    assert iterations > 0
    assert ack_retries >= 0
    serial_agent.drain_rx()

    warmup_errors = 0
    for _ in range(warmup_iterations):
        _, success, _ = heartbeat_with_retry(serial_agent, ack_timeout, ack_retries)
        if not success:
            warmup_errors += 1

    latencies_ms = []
    retry_count = 0
    error_counts = {"ack_timeout": 0, "write_timeout": 0, "serial_error": 0}
    measurement_start = time.perf_counter()
    for _ in range(iterations):
        start = time.perf_counter()
        retries_used, success, error = heartbeat_with_retry(
            serial_agent, ack_timeout, ack_retries
        )
        retry_count += retries_used
        if success:
            latencies_ms.append((time.perf_counter() - start) * 1000)
        else:
            error_counts[error] += 1
    measurement_duration_s = time.perf_counter() - measurement_start

    if not latencies_ms:
        print(
            f"[PERF REPORT]\n"
            f"- heartbeat ACK: count=0/{iterations}\n"
            f"- warmup_errors={warmup_errors} retries={retry_count}\n"
            f"- ack_timeouts={error_counts['ack_timeout']}\n"
            f"- write_timeouts={error_counts['write_timeout']}\n"
            f"- serial_errors={error_counts['serial_error']}"
        )
        return

    avg_ms = statistics.fmean(latencies_ms)
    p95_ms = percentile(latencies_ms, 95)
    worst_ms = max(latencies_ms)
    hz = 1000 / avg_ms if avg_ms else float("inf")
    message_rate = (
        len(latencies_ms) / measurement_duration_s
        if measurement_duration_s
        else float("inf")
    )

    print(
        f"[PERF REPORT]\n"
        f"- heartbeat ACK: count={len(latencies_ms)}/{iterations}\n"
        f"- avg={avg_ms:.2f}ms p95={p95_ms:.2f}ms worst={worst_ms:.2f}ms\n"
        f"- ack_rate={hz:.1f}Hz message_rate={message_rate:.1f}msg/s\n"
        f"- duration={measurement_duration_s:.3f}s retries={retry_count}\n"
        f"- warmup_errors={warmup_errors}\n"
        f"- ack_timeouts={error_counts['ack_timeout']}\n"
        f"- write_timeouts={error_counts['write_timeout']}\n"
        f"- serial_errors={error_counts['serial_error']}"
    )

    assert avg_ms <= max_avg_ms
    assert p95_ms <= max_p95_ms
