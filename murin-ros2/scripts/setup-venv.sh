#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
venv="${MURIN_VENV:-$root/.venv}"
python_bin="${PYTHON_BIN:-python3}"

if [[ ! -x "$venv/bin/python" ]]; then
  "$python_bin" -m venv --system-site-packages "$venv"
fi
"$venv/bin/python" -m pip install -e "${root}[dev]"
printf 'ROS Python environment ready: %s\n' "$venv"
