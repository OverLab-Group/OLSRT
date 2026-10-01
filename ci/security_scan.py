#!/usr/bin/env python3
"""
ci/security_scan.py — broader security scanners.

Backends: semgrep, snyk, trivy, grype.

This tool overlaps with static_analysis and secrets_check. It exists
so that a security-only pipeline can invoke a single command. The
individual backends run only when installed.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

def run_semgrep(reporter, verbose):
    if not has_tool("semgrep"):
        return False
    rc, out, err = run(
        ["semgrep", "--config=auto", "--error", "--quiet", "."],
        timeout=600)
    if rc != 0:
        reporter.fail("semgrep")
        if verbose:
            print(out + err)
    else:
        reporter.ok("semgrep")
    return True

def run_snyk(reporter, verbose):
    if not has_tool("snyk"):
        return False
    rc, out, err = run(["snyk", "test", "--all-projects"], timeout=600)
    if rc != 0:
        reporter.fail("snyk")
        if verbose:
            print(out + err)
    else:
        reporter.ok("snyk")
    return True

def run_trivy(reporter, verbose):
    if not has_tool("trivy"):
        return False
    rc, out, err = run(["trivy", "fs", "--exit-code", "1", "."],
                       timeout=600)
    if rc != 0:
        reporter.fail("trivy")
        if verbose:
            print(out + err)
    else:
        reporter.ok("trivy")
    return True

def run_grype(reporter, verbose):
    if not has_tool("grype"):
        return False
    rc, out, err = run(["grype", "dir:.", "--fail-on", "high"],
                       timeout=600)
    if rc != 0:
        reporter.fail("grype")
        if verbose:
            print(out + err)
    else:
        reporter.ok("grype")
    return True

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    r = Reporter("security_scan", verbose=args.verbose)
    ran = False
    for fn in (run_semgrep, run_snyk, run_trivy, run_grype):
        if fn(r, args.verbose):
            ran = True
    if not ran:
        r.skip("all security scanners", "none installed")
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
