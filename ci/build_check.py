#!/usr/bin/env python3
"""
ci/build_check.py — build the shared library with the default toolchain.

Optionally builds a static library and the demo binaries.
Exit code 0 iff every requested target builds.
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

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--static", action="store_true",
                    help="Also build a static library.")
    ap.add_argument("--demos", action="store_true",
                    help="Also build demos/.")
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args(argv)

    if not has_tool(args.cc):
        print("error: compiler %r not found" % args.cc, file=sys.stderr)
        return 2

    sources = sorted(str(p) for p in Path("src/code/streams").glob("*.c"))
    if not sources:
        print("error: no sources under src/code/streams/", file=sys.stderr)
        return 2

    r = Reporter("build_check", verbose=args.verbose)
    r.info("compiler: %s" % args.cc)
    r.info("sources: %d" % len(sources))
    print()

    with tempfile.TemporaryDirectory(prefix="olsrt_build_") as tmp:
        shared = os.path.join(tmp, "libolsrt.so")
        cmd = [args.cc, "-std=gnu11", "-fPIC", "-shared",
               "-O2", "-Wall", "-Wextra"]
        cmd += ["-I" + i for i in INCLUDES]
        cmd += sources
        cmd += ["-o", shared, "-lpthread", "-lrt", "-ldl"]
        rc, out, err = run(cmd)
        if rc != 0:
            r.fail("shared library")
            if args.verbose:
                print(out + err)
        else:
            r.ok("shared library")

        if args.static:
            static = os.path.join(tmp, "libolsrt.a")
            objs = []
            ok = True
            for src in sources:
                obj = os.path.join(
                    tmp, Path(src).stem + ".o")
                c = [args.cc, "-std=gnu11", "-fPIC", "-O2",
                     "-Wall", "-Wextra"]
                c += ["-I" + i for i in INCLUDES]
                c += ["-c", src, "-o", obj]
                rc, _, err = run(c)
                if rc != 0:
                    r.fail("static: compile %s" % Path(src).name)
                    ok = False
                    break
                objs.append(obj)
            if ok:
                rc, _, err = run(["ar", "rcs", static] + objs)
                if rc != 0:
                    r.fail("static library")
                else:
                    r.ok("static library")

    if args.demos:
        if not Path("demos/Makefile").exists():
            r.skip("demos", "no demos/Makefile")
        else:
            rc, out, err = run(["make", "-C", "demos", "clean"])
            rc, out, err = run(["make", "-C", "demos"])
            if rc != 0:
                r.fail("demos")
                if args.verbose:
                    print(out + err)
            else:
                r.ok("demos")

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
