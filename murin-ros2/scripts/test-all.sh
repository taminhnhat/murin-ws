#!/usr/bin/env bash
set -eo pipefail
MURIN_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$MURIN_ROOT/scripts/activate.sh"
cd "$MURIN_ROOT"
if [[ "${1:-}" == "--integration" ]]; then
  shift
  exec python3 -m pytest src/murin_control/test/test_socket_integration.py "$@"
fi
python3 -m unittest discover -s scripts/tests -v
colcon test --merge-install --packages-select murin_control "$@"
colcon test-result --test-result-base build/murin_control --verbose
