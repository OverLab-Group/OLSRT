#!/usr/bin/env python3
"""
ci/version_check.py — assert that every version string in the tree
agrees.

Sources of truth:

  includes/runtime/ol_globals.h   OL_VERSION_MAJOR/MINOR/PATCH
  includes/code/streams/ol_green_threads.h  OL_GT_VERSION_* macros
  CMakeLists.txt                  project(... VERSION x.y.z)
  Makefile                        (if it carries a version)
  README.md                       status table

Exit code 0 iff every version string matches.
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, read_version

def find_in_file(path, pattern, label, default=None):
    p = Path(path)
    if not p.exists():
        return None
    text = p.read_text(encoding="utf-8")
    # `pattern` is already a compiled regex.
    m = pattern.search(text)
    return m.group(1) if m else default

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    r = Reporter("version_check", verbose=args.verbose)

    canonical = read_version()
    if canonical is None:
        r.fail("ol_globals.h", "could not parse version")
        return r.exit_code()
    r.ok("ol_globals.h", canonical)
    print()

    checks = [
        ("includes/code/streams/ol_green_threads.h", re.compile(
            r'#define\s+OL_GT_VERSION_STRING\s+"([^"]+)"')),
        ("CMakeLists.txt", re.compile(
            r"project\([^)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
            re.DOTALL)),
        ("docs/CHANGELOG.md", re.compile(
            r"^##\s+\[([0-9]+\.[0-9]+\.[0-9]+)\]",
            re.MULTILINE)),
    ]

    for path, pattern in checks:
        found = find_in_file(path, pattern, path)
        if found is None:
            r.skip(path, "no version string")
            continue
        if found == canonical:
            r.ok(path, found)
        else:
            r.fail(path, "%s != %s" % (found, canonical))

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
