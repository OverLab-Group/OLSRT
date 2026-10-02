#!/usr/bin/env python3
"""
ci/commit_check.py - verify that the last N commits follow
Conventional Commits.

The subject must match:

  <type>(<scope>)?: <description>

Merge commits and commits whose subject begins with "wip:" are exempt,
because wip commits are allowed during a maintenance session and are
squashed before release.

Exit code 0 iff every non-exempt commit in the range matches.
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
    "actor", "network", "leaks", "wave1", "tools",
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
    if rc != 0:
        r.fail("git log", err.strip())
        return r.exit_code()

    lines = [ln for ln in out.splitlines() if ln.strip()]
    if not lines:
        r.fail("git log", "empty output")
        return r.exit_code()

    for ln in lines:
        parts = ln.split("\t", 1)
        if len(parts) != 2:
            continue
        sha, subject = parts
        short = sha[:7]

        # Exemptions.
        if subject.startswith("Merge "):
            r.skip("%s (merge)" % short)
            continue
        if subject.startswith("wip:"):
            r.skip("%s (wip)" % short)
            continue
        if subject.startswith("Revert "):
            r.ok("%s (revert)" % short)
            continue

        m = SUBJECT_RE.match(subject)
        if not m:
            r.fail(short, "not Conventional: %r" % subject[:60])
            continue
        t = m.group("type")
        if t not in TYPES:
            r.fail(short, "unknown type %r" % t)
            continue
        r.ok("%s %s" % (short, t))

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
