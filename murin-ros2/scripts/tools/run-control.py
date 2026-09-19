#!/usr/bin/env python3
"""Launch exactly one Murin ROS stack, with explicit serial opt-in."""

import argparse
import fcntl
import importlib.util
import math
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
from urllib.parse import urlparse
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[2]


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--hardware",
        action="store_true",
        help="Connect to the selected physical serial device; default is simulation",
    )
    parser.add_argument(
        "--serial-port", default=os.environ.get("MURIN_SERIAL_PORT", "/dev/murin-cdc")
    )
    parser.add_argument(
        "--baud-rate",
        type=int,
        choices=[115200, 921600, 1000000, 2000000],
        default=2000000,
    )
    parser.add_argument(
        "--server-url",
        default=os.environ.get("MURIN_SERVER_URL", "http://localhost:9091"),
    )
    parser.add_argument(
        "--wheel-radius",
        type=float,
        default=0.015,
        help="Metres; default is demo geometry",
    )
    parser.add_argument(
        "--wheel-separation",
        type=float,
        default=0.10,
        help="Metres; default is demo geometry",
    )
    parser.add_argument("--gui", action="store_true")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--no-bridge",
        action="store_true",
        help="Standalone ROS commands, no socket bridge watchdog",
    )
    mode.add_argument(
        "--external-bridge",
        action="store_true",
        help="Require the watchdog but run Python bridge separately",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Run startup checks without launching or opening serial ports",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print launch command only; no checks, network requests or hardware access",
    )
    args = parser.parse_args(argv)
    for name in ("wheel_radius", "wheel_separation"):
        if not math.isfinite(getattr(args, name)) or getattr(args, name) <= 0:
            parser.error(f"{name} must be finite and positive")
    parsed = urlparse(args.server_url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname:
        parser.error("--server-url must be an HTTP(S) server URL")
    return args


def command(args):
    return [
        "ros2",
        "launch",
        "murin_control",
        "murin.launch.py",
        f"gui:={str(args.gui).lower()}",
        f"transport:={'serial' if args.hardware else 'simulation'}",
        f"serial_port:={args.serial_port if args.hardware else ''}",
        f"baud_rate:={args.baud_rate}",
        f"enable_socket_bridge:={str(not args.no_bridge and not args.external_bridge).lower()}",
        f"require_bridge:={str(not args.no_bridge).lower()}",
        f"server_url:={args.server_url}",
        f"wheel_radius:={args.wheel_radius}",
        f"wheel_separation:={args.wheel_separation}",
    ]


def existing_stacks(proc=Path("/proc")):
    result = []
    for entry in proc.iterdir():
        if not entry.name.isdigit() or int(entry.name) == os.getpid():
            continue
        try:
            args = (entry / "cmdline").read_bytes().decode().split("\0")
        except (OSError, UnicodeError):
            continue
        if "murin_control" in args and "murin.launch.py" in args:
            result.append(entry.name)
    return result


def preflight(args):
    running = existing_stacks()
    if running:
        raise RuntimeError(
            f"Murin is already running (launch PID(s): {', '.join(running)}). Stop the existing launch cleanly before restarting; do not start a duplicate stack."
        )
    if not shutil.which("ros2"):
        raise RuntimeError("ROS is not active. Source scripts/activate.sh first.")
    if not (ROOT / "install/share/murin_control/package.xml").exists():
        raise RuntimeError("Murin is not built. Run scripts/build-all.sh first.")
    if args.hardware:
        port = Path(args.serial_port)
        if not port.exists() or not os.access(port, os.R_OK | os.W_OK):
            raise RuntimeError(f"Serial device is missing or inaccessible: {port}")
        if shutil.which("fuser"):
            owners = subprocess.run(
                ["fuser", str(port)], capture_output=True, text=True
            )
            if owners.returncode == 0:
                raise RuntimeError(
                    f"Serial port is already owned by PID(s): {owners.stdout.strip()}"
                )
    if not args.no_bridge and not args.external_bridge:
        if importlib.util.find_spec("socketio") is None:
            raise RuntimeError(
                "Install src/murin_control/requirements.txt into .venv (or set MURIN_VENV)."
            )
        # HTTP check only: connecting a dashboard socket and disconnecting it would latch estop.
        try:
            with urlopen(args.server_url, timeout=3) as response:
                response.read(1)
        except OSError as error:
            raise RuntimeError(
                f"Server is unreachable at {args.server_url}. Start murin-web in ROBOT_TRANSPORT=socket mode, and match HTTP_PORT. {error}"
            ) from error


def run_locked(args):
    (ROOT / "log").mkdir(exist_ok=True)
    with (ROOT / "log/murin-control.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError(
                "Another run-all.sh owns this workspace. Stop it before restarting."
            ) from error
        preflight(args)
        if args.check:
            print(
                "Startup checks passed. No serial device opened and no ROS process started."
            )
            return 0
        print("Starting:", shlex.join(command(args)), flush=True)
        if args.hardware:
            print(
                "Confirm wheel dimensions match the robot; defaults are demo values.",
                flush=True,
            )
        process = subprocess.Popen(
            command(args), cwd=ROOT, start_new_session=True, pass_fds=(lock.fileno(),)
        )

        def stop(signum, frame):
            if process.poll() is None:
                process.send_signal(signum)

        previous = {
            sig: signal.signal(sig, stop) for sig in (signal.SIGINT, signal.SIGTERM)
        }
        try:
            code = process.wait()
            return code if code >= 0 else 128 - code
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGTERM)
                    process.wait(timeout=5)
            for sig, handler in previous.items():
                signal.signal(sig, handler)


def main(argv=None):
    args = arguments(argv)
    if args.dry_run:
        print(shlex.join(command(args)))
        return 0
    try:
        return run_locked(args)
    except (OSError, RuntimeError) as error:
        print(f"Startup failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
