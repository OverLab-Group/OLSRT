#!/usr/bin/env python3
"""
ci/coverage_check.py — line coverage of the library by the test suite.

Uses whichever of gcov / lcov / llvm-cov is available. Prints the
total line coverage and fails if it is below --min (default 0, i.e.
report only).
"""

import argparse
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

INCLUDES = ["includes", "includes/code", "includes/code/streams",
            "includes/runtime"]

def build_with_coverage(tmp, cc, lib_sources):
    objs = []
    for src in lib_sources:
        obj = os.path.join(tmp, Path(src).stem + ".o")
        cmd = [cc, "-std=gnu11", "-fPIC", "-O0", "-g",
               "--coverage", "-fprofile-arcs", "-ftest-coverage"]
        cmd += ["-I" + i for i in INCLUDES]
        cmd += ["-c", src, "-o", obj]
        rc, out, err = run(cmd)
        if rc != 0:
            return None, obj, out + err
        objs.append(obj)
    lib = os.path.join(tmp, "libolsrt.so")
    cmd = [cc, "-shared", "--coverage"] + objs
    cmd += ["-o", lib, "-lpthread", "-lrt", "-ldl"]
    rc, out, err = run(cmd)
    if rc != 0:
        return None, lib, out + err
    return objs, lib, ""

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--min", type=float, default=0.0,
                    help="Minimum line coverage percentage.")
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args(argv)

    lib_sources = sorted(str(p) for p in
                         Path("src/code/streams").glob("*.c"))
    test_src = "tests/test_wave1.c"
    r = Reporter("coverage_check", verbose=args.verbose)

    if not Path(test_src).exists():
        r.skip("test_wave1", "not found")
        return r.exit_code()

    with tempfile.TemporaryDirectory(prefix="olsrt_cov_") as tmp:
        objs, lib, log = build_with_coverage(tmp, args.cc, lib_sources)
        if objs is None:
            r.fail("library with coverage")
            if args.verbose:
                print(log)
            return r.exit_code()

        exe = os.path.join(tmp, "test_wave1")
        cmd = [args.cc, "-std=gnu11", "-g", "--coverage"]
        cmd += ["-I" + i for i in INCLUDES]
        cmd += [test_src, "-L" + tmp, "-lolsrt",
                "-Wl,-rpath," + tmp, "-lpthread", "-lrt", "-ldl",
                "-o", exe]
        rc, out, err = run(cmd)
        if rc != 0:
            r.fail("test build")
            if args.verbose:
                print(out + err)
            return r.exit_code()

        rc, out, err = run([exe], timeout=120)
        if rc != 0:
            r.fail("test run")
            return r.exit_code()

        # Parse coverage. Try gcov first.
        total_lines = 0
        hit_lines = 0
        for src in lib_sources:
            base = Path(src).stem
            gcda_dir = tmp
            # gcov writes .gcov next to the object.
            for gcda in Path(tmp).glob(base + ".gcno"):
                pass
            # Invoke gcov on the object.
            obj = os.path.join(tmp, base + ".o")
            rc, out, err = run(["gcov", "-o", tmp, obj])
            for gcov_file in Path(".").glob(base + ".c.gcov"):
                for ln in gcov_file.read_text().splitlines():
                    parts = ln.split(":", 2)
                    if len(parts) < 3:
                        continue
                    head = parts[0].strip()
                    if head in ("-", ""):
                        continue
                    total_lines += 1
                    if head != "#####":
                        hit_lines += 1
                gcov_file.unlink()

        if total_lines == 0:
            r.skip("coverage", "no data (gcov failed)")
        else:
            pct = 100.0 * hit_lines / total_lines
            r.info("coverage: %.1f%% (%d/%d lines)"
                   % (pct, hit_lines, total_lines))
            if pct + 1e-6 >= args.min:
                r.ok("coverage >= %.1f%%" % args.min)
            else:
                r.fail("coverage >= %.1f%%" % args.min,
                       "got %.1f%%" % pct)

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
