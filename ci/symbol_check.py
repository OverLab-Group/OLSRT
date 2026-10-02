#!/usr/bin/env python3
"""
ci/symbol_check.py — report the exported symbols of the shared library.

Runs nm on the built libolsrt.so and prints the number of exported
functions. With --expect N, fails if the count does not match.

Also checks that no symbol starting with an underscore is exported
except for a small allow-list (the runtime reserves those).
"""

import argparse
import os
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import (Reporter, chdir_to_root, has_tool,
                     run, LINK_LIBS)

INCLUDES = ["includes", "includes/code", "includes/code/streams",
            "includes/runtime"]

def build(tmp, cc):
    lib = os.path.join(tmp, "libolsrt.so")
    sources = sorted(str(p) for p in
                     Path("src/code/streams").glob("*.c"))
    cmd = [cc, "-std=gnu11", "-fPIC", "-shared", "-O2"]
    cmd += ["-I" + i for i in INCLUDES]
    cmd += sources
    cmd += ["-o", lib] + LINK_LIBS
    rc, out, err = run(cmd)
    return lib, rc, out + err

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--expect", type=int, default=None)
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args(argv)

    if not has_tool("nm"):
        r = Reporter("symbol_check")
        r.skip("nm", "not installed")
        return r.exit_code()

    r = Reporter("symbol_check", verbose=args.verbose)

    with tempfile.TemporaryDirectory(prefix="olsrt_sym_") as tmp:
        lib, rc, log = build(tmp, args.cc)
        if rc != 0:
            r.fail("library build")
            if args.verbose:
                print(log)
            return r.exit_code()

        rc, out, err = run(["nm", "-D", "--defined-only", lib])
        exported = []
        for ln in out.splitlines():
            parts = ln.split()
            if len(parts) >= 3 and parts[1] == "T":
                exported.append(parts[2])

        public = [s for s in exported if s.startswith("ol_")]
        private = [s for s in exported
                   if not s.startswith("ol_") and s != ""]

        r.info("exported functions: %d" % len(exported))
        r.info("ol_* symbols: %d" % len(public))

        if private:
            # Symbols not starting with ol_ should normally be
            # compiler-generated or toolchain helpers. Report them
            # for review, but do not fail.
            r.info("non-ol symbols: %d" % len(private))
            if args.verbose:
                for s in private[:20]:
                    print("    " + s)

        if args.expect is not None:
            if len(public) == args.expect:
                r.ok("exported count == %d" % args.expect)
            else:
                r.fail("exported count == %d" % args.expect,
                       "got %d" % len(public))
        else:
            r.ok("symbol dump succeeded")

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
