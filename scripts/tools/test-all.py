#!/usr/bin/env python3
"""Run web syntax, socket, launcher, and formatting checks without hardware."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    node = shutil.which('node')
    if not node:
        parser.error('Node.js is required')
    sources = [ROOT / 'server/index.js', *sorted((ROOT / 'server/src').rglob('*.js')),
               *sorted((ROOT / 'server/public/js').glob('*.js'))]
    commands = [[node, '--check', str(path)] for path in sources]
    commands += [
        [node, '--test', *map(str, sorted((ROOT / 'server/test').glob('*.test.js')))],
        [sys.executable, '-m', 'unittest', 'discover', '-s', 'scripts/tests', '-v'],
        [sys.executable, str(ROOT / 'scripts/tools/format-all.py'), '--check'],
    ]
    failed = False
    for cmd in commands:
        result = subprocess.run(cmd, cwd=ROOT)
        failed |= result.returncode != 0
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
