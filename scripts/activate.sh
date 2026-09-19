#!/usr/bin/env bash
# Source this file; activation changes the caller's environment.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  printf 'Use: source %s\n' "$0" >&2
  exit 1
fi

_murin_activate() {
  local root setup venv result=0 restore_nounset=false
  root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)" || return
  setup="${MURIN_ROS_SETUP:-/opt/ros/jazzy/setup.bash}"
  venv="${MURIN_VENV:-$root/.venv}"
  if [[ ! -r "$setup" ]]; then
    printf 'ROS setup not found: %s\n' "$setup" >&2
    return 1
  fi
  # ROS-generated setup scripts may reference unset shell variables.
  [[ $- == *u* ]] && restore_nounset=true
  set +u
  source "$setup" || result=$?
  if [[ $result -eq 0 && -r "$venv/bin/activate" ]]; then
    source "$venv/bin/activate" || result=$?
  elif [[ $result -eq 0 && -n "${MURIN_VENV:-}" ]]; then
    printf 'Virtual environment not found: %s\n' "$venv" >&2
    result=1
  fi
  if [[ $result -eq 0 && -r "$root/install/setup.bash" ]]; then
    source "$root/install/setup.bash" || result=$?
  fi
  if $restore_nounset; then set -u; fi
  return "$result"
}
_murin_activate
