#!/usr/bin/env python3
"""
ci/secrets_check.py — scan the tree for secrets.

Backends: gitleaks, trufflehog.

Note: this is a read-only scanner. It does not touch the git history
unless --deep is passed.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

def run_gitleaks(reporter, verbose, deep):
    if not has_tool("gitleaks"):
        return False
    cmd = ["gitleaks", "detect", "--no-banner", "--redact"]
    if not deep:
        cmd += ["--no-git"]
    cmd += ["--source=."]
    rc, out, err = run(cmd, timeout=300)
    combined = (out + err).lower()
    if rc != 0 or "leak" in combined:
        reporter.fail("gitleaks")
        if verbose:
            print(out + err)
    else:
        reporter.ok("gitleaks")
    return True

def run_trufflehog(reporter, verbose, deep):
    if not has_tool("trufflehog"):
        return False
    cmd = ["trufflehog", "filesystem", "--fail", "."]
    rc, out, err = run(cmd, timeout=300)
    if rc != 0:
        reporter.fail("trufflehog")
        if verbose:
            print(out + err)
    else:
        reporter.ok("trufflehog")
    return True

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--deep", action="store_true",
                    help="Also scan git history.")
    args = ap.parse_args(argv)

    r = Reporter("secrets_check", verbose=args.verbose)
    ran = False
    for fn in (run_gitleaks, run_trufflehog):
        if fn(r, args.verbose, args.deep):
            ran = True
    if not ran:
        r.skip("all secret scanners", "none installed")
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
