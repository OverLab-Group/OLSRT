#!/usr/bin/env python3
"""
ci/spelling_check.py — run every detected spell checker.

Backends: codespell, typos.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

def run_codespell(reporter, verbose):
    if not has_tool("codespell"):
        return False
    cmd = ["codespell", "--quiet-level=2", "--check-filenames",
           "src", "includes", "tests", "demos", "docs",
           "--skip=build,bin,.git,__pycache__,future"]
    rc, out, err = run(cmd, timeout=120)
    if rc != 0:
        reporter.fail("codespell")
        if verbose:
            print(out + err)
    else:
        reporter.ok("codespell")
    return True

def run_typos(reporter, verbose):
    if not has_tool("typos"):
        return False
    cmd = ["typos", "--exclude", "future/", "--exclude", "build/",
           "--exclude", "bin/"]
    rc, out, err = run(cmd, timeout=120)
    if rc != 0:
        reporter.fail("typos")
        if verbose:
            print(out + err)
    else:
        reporter.ok("typos")
    return True

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    r = Reporter("spelling_check", verbose=args.verbose)
    ran = False
    for fn in (run_codespell, run_typos):
        if fn(r, args.verbose):
            ran = True
    if not ran:
        r.skip("all spell checkers", "none installed")
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
