#!/usr/bin/env python3
"""List pointer-to-smaller-integer casts in the native build.

With -fms-extensions (needed by the engine), clang accepts reinterpret_cast<int>(p) and
(int)p silently, even under -Weverything; without it they are errors. This re-runs every
command of compile_commands.json as -fsyntax-only without MS extensions and keeps only
those errors. Other errors caused by dropping the extensions are ignored.

Usage (inside the gta2-pm container): scan_pointer_truncation.py BUILD_DIR
"""
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys

PATTERN = re.compile(r'^(\S+):(\d+):\d+: error: cast from pointer to smaller type .*$', re.M)


def scan(entry):
    args = shlex.split(entry['command'])
    out = []
    skip = False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ('-o', '-MF', '-MT'):
            skip = True
            continue
        if a in ('-c', '-MD') or a == '-fms-extensions':
            continue
        out.append(a)
    out += ['-fsyntax-only', '-fno-ms-extensions', '-ferror-limit=0']
    r = subprocess.run(out, cwd=entry['directory'], capture_output=True, text=True)
    return PATTERN.findall(r.stderr) and [m.group(0) for m in PATTERN.finditer(r.stderr)]


def main(argv):
    with open(os.path.join(argv[1], 'compile_commands.json')) as f:
        entries = json.load(f)
    found = set()
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
        for lines in ex.map(scan, entries):
            found.update(lines or [])
    for line in sorted(found):
        print(line.replace('/workspace/', ''))
    print(f'{len(found)} pointer truncations', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
