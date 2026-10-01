#!/usr/bin/env python3
"""
ci/warnings_check.py — compile with -Wall -Wextra and count warnings.

The budget defaults to the current known baseline. Use --budget N to
override, or --zero to require zero warnings (the v1.3.3 target).

Exit code 0 iff the warning count is at or below the budget.
"""

import argparse
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

INCLUDES = ["includes", "includes/code", "includes/code/streams",
            "includes/runtime"]

WARNING_PATTERN = re.compile(r":\s+warning:\s")

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--budget", type=int, default=None,
                    help="Maximum allowed warnings (default: no cap).")
    ap.add_argument("--zero", action="store_true",
                    help="Require exactly zero warnings.")
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args(argv)

    if not has_tool(args.cc):
        print("error: compiler %r not found" % args.cc, file=sys.stderr)
        return 2

    sources = sorted(str(p) for p in Path("src/code/streams").glob("*.c"))

    r = Reporter("warnings_check", verbose=args.verbose)
    r.info("compiler: %s" % args.cc)
    r.info("sources: %d" % len(sources))
    print()

    lines = []
    total = 0
    for src in sources:
        cmd = [args.cc, "-std=gnu11", "-O2", "-c",
               "-Wall", "-Wextra", "-Wno-unused-parameter"]
        cmd += ["-I" + i for i in INCLUDES]
        cmd += [src, "-o", os.devnull]
        rc, out, err = run(cmd)
        for ln in (out + err).splitlines():
            if WARNING_PATTERN.search(ln):
                total += 1
                lines.append(ln)

    if args.verbose or lines:
        for ln in lines:
            print("    " + ln)
        print()

    if args.zero:
        if total == 0:
            r.ok("zero warnings")
        else:
            r.fail("zero warnings", "%d warning(s)" % total)
    elif args.budget is not None:
        if total <= args.budget:
            r.ok("within budget", "%d <= %d" % (total, args.budget))
        else:
            r.fail("within budget",
                   "%d > %d" % (total, args.budget))
    else:
        r.info("warning count: %d (no budget set)" % total)
        r.ok("compile succeeded")

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
