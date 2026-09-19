#!/usr/bin/env bash
set -eo pipefail
MURIN_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$MURIN_ROOT/scripts/activate.sh"
cd "$MURIN_ROOT"
# Jazzy's apt-installed build dependencies belong to the system interpreter.
# Set this explicitly so an isolated venv or an old CMake cache cannot select
# a Python that cannot import catkin_pkg and the ROS build tools.
if [[ $# -eq 0 ]]; then
  set -- --packages-up-to murin_control
fi
exec colcon build --merge-install \
  --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3 "$@"
