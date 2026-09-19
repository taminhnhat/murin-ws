"""Pytest configuration for pytest tests."""

import time
import sys
import logging
from pathlib import Path

import pytest
import yaml


# Add utils directory to path
sys.path.insert(0, str(Path(__file__).parent.parent.parent / "utils"))

from protocol_serial import SerialAgent

CONFIG_PATH = Path(__file__).with_name("test_config.yaml")


def pytest_configure(config):
    """Show serial protocol logs only for highly verbose pytest runs."""
    if config.getoption("verbose") >= 3:
        config.option.log_cli = True
        config.option.log_cli_level = "DEBUG"
        logging.getLogger("protocol_serial").setLevel(logging.DEBUG)


@pytest.fixture(scope="session")
def test_config():
    try:
        with CONFIG_PATH.open("r", encoding="utf-8") as f:
            return yaml.safe_load(f) or {}
    except FileNotFoundError:
        return {}


@pytest.fixture(scope="session")
def serial_agent(test_config, pytestconfig):
    port = test_config.get("cdc_port")
    if not port:
        pytest.skip(f"No serial port configured in {CONFIG_PATH}")

    baudrate = test_config.get("baudrate", 115200)
    agent = SerialAgent(port, baudrate)
    try:
        agent.connect()
    except RuntimeError as exc:
        # pytest hides skip reasons unless ``-rs`` is used.  Print the
        # connection failure as well so a missing or unavailable COM port is
        # visible in the normal test output.
        cause = exc.__cause__
        detail = f"{exc}: {cause}" if cause else str(exc)
        message = f"Unable to open serial port {port}: {detail}"
        pytestconfig.get_terminal_writer().line(message, red=True)
        pytest.skip(message)
    agent.start_rx()
    time.sleep(0.2)

    yield agent
    agent.close()
