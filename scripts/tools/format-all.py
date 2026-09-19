#!/usr/bin/env python3
"""Format project sources, or check formatting without changing files."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
VENV_BIN = ROOT / ".venv" / ("Scripts" if os.name == "nt" else "bin")
EXCLUDED = {
    ".git",
    ".venv",
    "venv",
    "__pycache__",
    ".pytest_cache",
    "node_modules",
    "managed_components",
}
CPP_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx"}


def source_files(root=ROOT):
    groups = {"cpp": [], "python": [], "cmake": []}
    # Explicit roots keep the upstream demos and other vendored packages untouched.
    for target in (root / "src/murin_control", root / "scripts"):
        if target.is_symlink():
            continue
        for directory, dirs, files in os.walk(target):
            dirs[:] = sorted(
                d
                for d in dirs
                if d not in EXCLUDED | {"install", "log", ".ruff_cache"}
                and not d.startswith("build")
                and not (Path(directory) / d).is_symlink()
            )
            for name in sorted(files):
                path = Path(directory) / name
                if path.is_symlink():
                    continue
                if path.suffix in CPP_SUFFIXES:
                    groups["cpp"].append(path)
                elif path.suffix == ".py":
                    groups["python"].append(path)
                elif name == "CMakeLists.txt" or path.suffix == ".cmake":
                    groups["cmake"].append(path)
                elif not path.suffix:
                    with path.open("rb") as stream:
                        first_line = stream.readline(256)
                    if first_line.startswith(b"#!") and b"python" in first_line:
                        groups["python"].append(path)
    return groups


def commands(groups, check=False):
    for path in groups["cpp"]:
        yield [
            "clang-format",
            "--style=file",
            *(["--dry-run", "--Werror"] if check else ["-i"]),
            str(path),
        ]
    for path in groups["python"]:
        yield ["ruff", "format", *(["--check"] if check else []), str(path)]
    for path in groups["cmake"]:
        yield ["cmake-format", "--check" if check else "-i", str(path)]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    if VENV_BIN.is_dir():
        os.environ["PATH"] = os.pathsep.join(
            [str(VENV_BIN), os.environ.get("PATH", "")]
        )
    planned = list(commands(source_files(), args.check))
    missing = sorted({cmd[0] for cmd in planned if not shutil.which(cmd[0])})
    if missing:
        print("Missing formatting tools: " + ", ".join(missing), file=sys.stderr)
        return 1
    failed = False
    for cmd in planned:
        result = subprocess.run(cmd, cwd=ROOT)
        failed |= result.returncode != 0
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
