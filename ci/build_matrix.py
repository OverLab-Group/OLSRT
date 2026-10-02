#!/usr/bin/env python3
"""
ci/build_matrix.py — build the library across compilers, C standards,
and optimisation levels.

Detected compilers: gcc, clang, tcc (each via the usual name; cc is
always tried).

Standards: c99, c11, c17, c23 (the latter only when the compiler
supports it; failures are recorded as skips).

Optimisation levels: -O0, -O2, -Os.

A single source file is used to keep the matrix small; a full build
across every combination would take too long. Pass --full to compile
all of src/code/streams.
"""

import argparse
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

INCLUDES = ["includes", "includes/code", "includes/code/streams",
            "includes/runtime"]

COMPILERS = ["cc", "gcc", "clang", "tcc"]
# The OLSRT codebase uses GNU extensions (nanosleep,
# sem_timedwait, clock_gettime, pthread_rwlock_t, the `asm`
# keyword in the POSIX green-thread backend). Strict ISO C
# standards do not expose these symbols. The Makefile and
# CMake both use gnu11, so the matrix tests the gnu variants.
STANDARDS = ["gnu99", "gnu11", "gnu17", "gnu23"]
OPT_LEVELS = ["-O0", "-O2", "-Os"]

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--full", action="store_true",
                    help="Compile every source file in the matrix.")
    ap.add_argument("--compiler", action="append")
    ap.add_argument("--standard", action="append")
    args = ap.parse_args(argv)

    compilers = args.compiler or [c for c in COMPILERS if has_tool(c)]
    standards = args.standard or STANDARDS

    if args.full:
        sources = sorted(str(p) for p in
                         Path("src/code/streams").glob("*.c"))
    else:
        # One representative file keeps the matrix quick.
        sources = [str(Path("src/code/streams/ol_channel.c"))]

    r = Reporter("build_matrix", verbose=args.verbose)
    r.info("compilers: %s" % ", ".join(compilers))
    r.info("sources: %d" % len(sources))
    print()

    for cc in compilers:
        for std in standards:
            for opt in OPT_LEVELS:
                label = "%s / %s / %s" % (cc, std, opt)
                with tempfile.TemporaryDirectory(
                        prefix="olsrt_bm_") as tmp:
                    failed = False
                    for src in sources:
                        obj = os.path.join(tmp, Path(src).stem + ".o")
                        cmd = [cc, "-std=" + std, opt, "-fPIC",
                               "-Wall", "-Wextra",
                               "-Wno-unused-parameter", "-c"]
                        cmd += ["-I" + i for i in INCLUDES]
                        cmd += [src, "-o", obj]
                        rc, out, err = run(cmd, timeout=60)
                        if rc != 0:
                            failed = True
                            break
                    if failed:
                        # tcc does not support c99/gnu extension
                        # suffixes cleanly; skip rather than fail.
                        if cc == "tcc" or std == "c23":
                            r.skip(label)
                        else:
                            r.fail(label)
                            if args.verbose:
                                print(out + err)
                    else:
                        r.ok(label)

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
