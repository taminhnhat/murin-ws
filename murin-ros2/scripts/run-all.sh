#!/usr/bin/env bash
set -eo pipefail
MURIN_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$MURIN_ROOT/scripts/activate.sh"
exec python3 "$MURIN_ROOT/scripts/tools/run-control.py" "$@"
