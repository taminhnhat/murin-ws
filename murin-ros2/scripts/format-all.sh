#!/usr/bin/env bash
set -euo pipefail
# Formatting needs Python and formatter tools, but does not require ROS activation.
exec python3 "$(dirname -- "${BASH_SOURCE[0]}")/tools/format-all.py" "$@"
