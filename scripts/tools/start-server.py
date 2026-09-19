#!/usr/bin/env python3
"""Create local configuration when needed, then start the Node server."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent.parent


def read_settings(contents):
    settings = {}
    for line in contents.splitlines():
        match = re.match(r"^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$", line)
        if not match:
            continue
        key, value = match.groups()
        if value.startswith(('"', "'", '`')):
            value = value[1:].split(value[0], 1)[0]
        else:
            value = value.split('#', 1)[0].strip()
        settings[key] = value
    return settings


def check_configuration(root=ROOT, environ=None):
    environ = os.environ if environ is None else environ
    filename = root / "server" / ".env"
    if not filename.exists():
        template = (filename.parent / ".env.example").read_text()
        try:
            fd = os.open(filename, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        except FileExistsError:
            pass
        else:
            with os.fdopen(fd, "w") as output:
                output.write(template)
            print(f"Created {filename}")

    contents = filename.read_text()
    settings = {**read_settings(contents), **environ}
    transport = settings.get("ROBOT_TRANSPORT", "").strip().lower()
    if transport not in ("serial", "socket"):
        return filename, ["ROBOT_TRANSPORT (must be serial or socket)"]
    port = settings.get("HTTP_PORT", "9091")
    if not port.isdecimal() or not 1 <= int(port) <= 65535:
        return filename, ["HTTP_PORT (must be 1..65535)"]
    if transport == "socket":
        return filename, []
    missing = []
    if not settings.get("USB_PORT", "").strip():
        missing.append("USB_PORT")
    if not settings.get("CONSOLE_PORT", "").strip():
        missing.append("CONSOLE_PORT")
    return filename, missing


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Validate/create local configuration without starting the server or opening ports")
    options, server_args = parser.parse_known_args()
    try:
        filename, missing = check_configuration()
    except OSError as error:
        print(f"Setup failed: {error}", file=sys.stderr)
        return 1
    if missing:
        print(
            f"Set {' and '.join(missing)} in {filename}, then run this command again.",
            file=sys.stderr,
        )
        return 1
    if options.check:
        print(f"Configuration valid: {filename}. Server was not started.")
        return 0
    npm = shutil.which("npm.cmd" if os.name == "nt" else "npm")
    if not npm:
        print('npm was not found. Install Node.js and npm first.', file=sys.stderr)
        return 1
    command = [npm, "start", "--", *server_args]
    os.chdir(ROOT)
    if os.name != 'nt':
        os.execv(npm, command)
    try:
        return subprocess.call(command)
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
