#!/usr/bin/env python3
"""Case-insensitive #include aliases for the native build.

Same job as make_case_aliases() in scripts/build_port.py, which it reuses: the sources
were written on case-insensitive filesystems, so `#include "Fix16.hpp"` misses
`fix16.hpp` on a case-sensitive one. Symlinks are created in OUT only for includes
that do not resolve as written (none on a case-insensitive mount such as Docker
Desktop's on macOS).

Usage: case_aliases.py OUT INCLUDE_DIR...
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # no scripts/__pycache__ in the repo
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_port  # noqa: E402  (stdlib only; main() is not run on import)


def main(argv):
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    build_port.CASE_ALIASES = Path(argv[1]).resolve()
    build_port.INCLUDE_DIRS = tuple(Path(d).resolve() for d in argv[2:])
    build_port.make_case_aliases()
    build_port.CASE_ALIASES.mkdir(parents=True, exist_ok=True)  # CMake expects the directory
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
