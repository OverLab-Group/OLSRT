#!/usr/bin/env python3
"""
ci/include_check.py — include hygiene.

Two independent checks:

1. Every public header under includes/ must be self-contained: it
   compiles when included on its own in a fresh translation unit.
2. If include-what-you-use (iwyu) is available, run it against the
   library sources.

The self-containment check is the source of truth; iwyu is
supplementary.
"""

import argparse
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

INCLUDE_DIRS = ["includes", "includes/code", "includes/code/streams",
                "includes/runtime"]

def headers():
    result = []
    for top in ("includes",):
        for p in sorted(Path(top).rglob("*.h")):
            result.append(str(p))
    return result

def self_contained(reporter, verbose):
    hdrs = headers()
    if not hdrs:
        reporter.skip("self-contained headers", "no headers found")
        return
    cc = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory(prefix="olsrt_hdr_") as tmp:
        bad = 0
        for hdr in hdrs:
            src = os.path.join(tmp, "probe.c")
            with open(src, "w") as f:
                f.write('#include "%s"\nint main(void){return 0;}\n'
                        % hdr)
            cmd = [cc, "-std=gnu11", "-fsyntax-only"]
            cmd += ["-I" + i for i in INCLUDE_DIRS]
            cmd += [src]
            rc, _, err = run(cmd)
            if rc != 0:
                bad += 1
                if verbose:
                    print("    %s" % hdr)
        if bad:
            reporter.fail("self-contained headers",
                          "%d of %d" % (bad, len(hdrs)))
        else:
            reporter.ok("self-contained headers",
                        "%d header(s)" % len(hdrs))

def run_iwyu(reporter, verbose):
    if not has_tool("include-what-you-use"):
        reporter.skip("iwyu", "not installed")
        return
    sources = sorted(str(p) for p in Path("src/code/streams").glob("*.c"))
    bad = 0
    for src in sources:
        cmd = ["include-what-you-use", "-std=gnu11"]
        cmd += ["-I" + i for i in INCLUDE_DIRS]
        cmd += [src]
        rc, out, err = run(cmd, timeout=120)
        if "should add these lines" in out + err:
            bad += 1
            if verbose:
                print("    " + src)
    if bad:
        reporter.info("iwyu: %d file(s) with suggestions" % bad)
        reporter.ok("iwyu (informational)")
    else:
        reporter.ok("iwyu")

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args(argv)

    if args.list:
        for h in headers():
            print(h)
        return 0

    r = Reporter("include_check", verbose=args.verbose)
    self_contained(r, args.verbose)
    run_iwyu(r, args.verbose)
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
