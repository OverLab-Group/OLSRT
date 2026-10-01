#!/usr/bin/env python3
"""
ci/valgrind_check.py — run the test suite under every valgrind tool
that is installed.

Backends: memcheck, helgrind, drd, massif.

Memcheck and helgrind are the primary tools. Drd overlaps with
helgrind and massif is informational.
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

def build_plain(cc, tmp, lib_sources):
    out = os.path.join(tmp, "libolsrt.so")
    cmd = [cc, "-std=gnu11", "-fPIC", "-shared", "-g", "-O0",
           "-fno-omit-frame-pointer"]
    cmd += ["-I" + i for i in INCLUDES]
    cmd += lib_sources
    cmd += ["-o", out, "-lpthread", "-lrt", "-ldl"]
    return run(cmd), out

def build_test(cc, tmp, src, lib):
    exe = os.path.join(tmp, Path(src).stem)
    cmd = [cc, "-std=gnu11", "-g", "-O0",
           "-I" + INCLUDES[0]]
    cmd = [cc, "-std=gnu11", "-g", "-O0"]
    cmd += ["-I" + i for i in INCLUDES]
    cmd += [src, "-L" + tmp, "-lolsrt",
            "-Wl,-rpath," + tmp, "-lpthread", "-lrt", "-ldl",
            "-o", exe]
    return run(cmd), exe

def run_tool(reporter, tool_name, exe, verbose, extra_args=None):
    cmd = ["valgrind", "--tool=" + tool_name, "--error-exitcode=1",
           "--quiet"]
    if extra_args:
        cmd += extra_args
    cmd += [exe]
    rc, out, err = run(cmd, timeout=600)
    combined = out + err
    if rc != 0:
        reporter.fail("valgrind %s" % tool_name)
        if verbose:
            print(combined)
    else:
        reporter.ok("valgrind %s" % tool_name)

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args(argv)

    if not has_tool("valgrind"):
        r = Reporter("valgrind_check")
        r.skip("all valgrind tools", "valgrind not installed")
        return r.exit_code()

    lib_sources = sorted(str(p) for p in
                         Path("src/code/streams").glob("*.c"))
    tests = [t for t in ["tests/test_wave1.c"] if Path(t).exists()]

    r = Reporter("valgrind_check", verbose=args.verbose)

    with tempfile.TemporaryDirectory(prefix="olsrt_valg_") as tmp:
        rc, out, err = build_plain(args.cc, tmp, lib_sources)
        if rc != 0:
            r.fail("library build")
            if args.verbose:
                print(out + err)
            return r.exit_code()

        # Use test_wave1 as the primary driver.
        for src in tests:
            rc, out, err = build_test(args.cc, tmp, src, None)
            if rc != 0:
                r.fail("build %s" % Path(src).stem)
                continue
            exe = os.path.join(tmp, Path(src).stem)
            for tool, extra in [
                ("memcheck", ["--leak-check=full",
                              "--show-leak-kinds=all"]),
                ("helgrind", None),
                ("drd", None),
            ]:
                run_tool(r, tool, exe, args.verbose, extra)

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
