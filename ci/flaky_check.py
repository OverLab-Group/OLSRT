#!/usr/bin/env python3
"""
ci/flaky_check.py — run the test suite N times and report variance.

Uses the plain build produced by build_check. Each iteration runs the
test binary in a fresh process. If any run fails while previous runs
passed, the test is flagged as flaky.
"""

import argparse
import os
import sys
import tempfile
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

INCLUDES = ["includes", "includes/code", "includes/code/streams",
            "includes/runtime"]

TESTS = ["tests/test_wave1.c"]

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("-n", "--runs", type=int, default=5)
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args(argv)

    r = Reporter("flaky_check", verbose=args.verbose)

    lib_sources = sorted(str(p) for p in
                         Path("src/code/streams").glob("*.c"))

    with tempfile.TemporaryDirectory(prefix="olsrt_flaky_") as tmp:
        lib = os.path.join(tmp, "libolsrt.so")
        cmd = [args.cc, "-std=gnu11", "-fPIC", "-shared", "-O2"]
        cmd += ["-I" + i for i in INCLUDES]
        cmd += lib_sources
        cmd += ["-o", lib, "-lpthread", "-lrt", "-ldl"]
        rc, out, err = run(cmd)
        if rc != 0:
            r.fail("library build")
            if args.verbose:
                print(out + err)
            return r.exit_code()

        for src in TESTS:
            if not Path(src).exists():
                continue
            exe = os.path.join(tmp, Path(src).stem)
            cmd = [args.cc, "-std=gnu11", "-O2"]
            cmd += ["-I" + i for i in INCLUDES]
            cmd += [src, "-L" + tmp, "-lolsrt",
                    "-Wl,-rpath," + tmp, "-lpthread", "-lrt",
                    "-ldl", "-o", exe]
            rc, out, err = run(cmd)
            if rc != 0:
                r.fail("build %s" % Path(src).stem)
                continue

            codes = Counter()
            for i in range(args.runs):
                rc, _, _ = run([exe], timeout=120)
                codes[rc] += 1

            if len(codes) == 1:
                r.ok("%s (%d runs)" % (Path(src).stem, args.runs))
            else:
                r.fail("%s is flaky" % Path(src).stem,
                       "exit codes: %s" % dict(codes))

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
