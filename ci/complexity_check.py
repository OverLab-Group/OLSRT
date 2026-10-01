#!/usr/bin/env python3
"""
ci/complexity_check.py — cyclomatic complexity with lizard or pmccabe.

The default budget is 30 per function. Override with --budget.
Exit code 0 iff every function is at or below the budget.
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

def sources():
    return sorted(str(p) for p in Path("src/code/streams").glob("*.c"))

def run_lizard(reporter, verbose, budget):
    if not has_tool("lizard"):
        return False
    cmd = ["lizard", "-C", str(budget), "-w"] + sources()
    rc, out, err = run(cmd, timeout=120)
    if rc != 0:
        reporter.fail("lizard", "complexity over %d" % budget)
        if verbose:
            print(out + err)
    else:
        reporter.ok("lizard", "budget %d" % budget)
    return True

def run_pmccabe(reporter, verbose, budget):
    if not has_tool("pmccabe"):
        return False
    cmd = ["pmccabe", "-c"] + sources()
    rc, out, err = run(cmd, timeout=60)
    over = 0
    for ln in out.splitlines():
        parts = ln.split()
        if len(parts) < 2:
            continue
        try:
            score = int(parts[0])
        except ValueError:
            continue
        if score > budget:
            over += 1
    if over:
        reporter.fail("pmccabe", "%d function(s) over %d" % (over, budget))
    else:
        reporter.ok("pmccabe", "budget %d" % budget)
    return True

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--budget", type=int, default=30)
    args = ap.parse_args(argv)

    r = Reporter("complexity_check", verbose=args.verbose)
    ran = False
    for fn in (run_lizard, run_pmccabe):
        if fn(r, args.verbose, args.budget):
            ran = True
    if not ran:
        r.skip("complexity backends", "none installed")
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
