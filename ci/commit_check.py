#!/usr/bin/env python3
"""
ci/commit_check.py — verify that the last N commits follow Conventional
Commits.

The subject must match:

  <type>(<scope>)?: <description>

where <type> is one of the types listed below. Merge commits are
exempt. The default scan depth is 10; use --n to change it.
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, run

TYPES = {
    "feat", "fix", "docs", "style", "refactor", "perf", "test",
    "build", "ci", "chore", "revert",
    "actor", "network", "leaks", "wave1", "tools", "wip",
}

SUBJECT_RE = re.compile(
    r"^(?P<type>[a-z]+)"
    r"(?:\((?P<scope>[^)]+)\))?"
    r"(?P<bang>!)?"
    r":\s+(?P<desc>.+)$"
)

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("-n", "--n", type=int, default=10,
                    help="How many recent commits to inspect.")
    args = ap.parse_args(argv)

    r = Reporter("commit_check", verbose=args.verbose)

    rc, out, err = run(
        ["git", "log", "--format=%H%x09%s", "-n", str(args.n)])
    if rc
