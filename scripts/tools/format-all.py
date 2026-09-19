#!/usr/bin/env python3
"""Format or check Murin web files with Prettier."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent.parent
TARGETS = (
    "package.json", "package-lock.json", "server/**/*.js", "server/**/*.json",
    "server/**/*.css", "server/**/*.html", "server/**/*.md",
    "server/**/*.yaml", "server/**/*.yml",
)

def prettier_command():
    executable = "prettier.cmd" if os.name == "nt" else "prettier"
    local = ROOT / "node_modules" / ".bin" / executable
    if local.is_file():
        print(f"Using local Prettier: {local}")
        return [str(local)]
    npx = shutil.which("npx.cmd" if os.name == "nt" else "npx")
    if not npx:
        raise RuntimeError("Prettier is not installed and npx is unavailable. Install Node.js and project dependencies first.")
    print("Local Prettier not found; using prettier@3.6.2 through npx.")
    return [npx, "--yes", "prettier@3.6.2"]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="check formatting without writing files")
    args = parser.parse_args()
    command = prettier_command()
    command.extend([
        "--check" if args.check else "--write", "--ignore-unknown",
        "--no-error-on-unmatched-pattern", "--ignore-path", str(ROOT / ".gitignore"),
        *TARGETS,
    ])
    return subprocess.call(command, cwd=ROOT)

if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
