#!/usr/bin/env python3
"""
ci/sanitizer_matrix.py — build and run the test suite under each
sanitizer in isolation.

Configurations:

  ASan   address sanitizer, no LSan
  LSan   leak sanitizer only (must be combined with ASan on Linux)
  UBSan  undefined behavior sanitizer
  MSan   memory sanitizer (requires instrumented libc; usually skipped)
  TSan   thread sanitizer

verify.py already runs ASan+UBSan+LSan together and TSan separately.
This tool isolates each one so that a failure cannot be masked by a
sibling sanitizer. Prefer this tool when debugging a specific class of
bug; prefer verify.py for the release gate.
"""

import argparse
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import (Reporter, chdir_to_root, has_tool,
                     run, LINK_LIBS)

INCLUDES = ["includes", "includes/code", "includes/code/streams",
            "includes/runtime"]

TESTS = ["tests/test_wave1.c"]

CONFIGS = [
    ("asan", "-fsanitize=address -fno-omit-frame-pointer -g -O1",
     {"ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1"}),
    ("lsan", "-fsanitize=address -fno-omit-frame-pointer -g -O1",
     {"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1"}),
    ("ubsan", "-fsanitize=undefined -fno-omit-frame-pointer -g -O1",
     {"UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"}),
    ("msan", "-fsanitize=memory -fno-omit-frame-pointer -g -O1",
     {"MSAN_OPTIONS": "halt_on_error=1"}),
    ("tsan", "-fsanitize=thread -fno-omit-frame-pointer -g -O1",
     {"TSAN_OPTIONS": "halt_on_error=1"}),
]

def build(tmp, cc, flags, sources, out_name):
    out = os.path.join(tmp, out_name)
    cmd = [cc, "-std=gnu11", "-fPIC", "-shared"]
    cmd += flags.split()
    cmd += ["-I" + i for i in INCLUDES]
    cmd += sources
    cmd += ["-o", out] + LINK_LIBS
    return run(cmd)

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--only", action="append")
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args(argv)

    if not has_tool(args.cc):
        print("error: compiler %r not found" % args.cc, file=sys.stderr)
        return 2

    lib_sources = sorted(str(p) for p in
                         Path("src/code/streams").glob("*.c"))
    wanted = set(args.only) if args.only else None
    configs = [c for c in CONFIGS if not wanted or c[0] in wanted]

    r = Reporter("sanitizer_matrix", verbose=args.verbose)

    for name, flags, env_over in configs:
        with tempfile.TemporaryDirectory(prefix="olsrt_san_") as tmp:
            rc, out, err = build(tmp, args.cc, flags, lib_sources,
                                 "libolsrt.so")
            if rc != 0:
                # MSan usually fails here on glibc; report as skip.
                if name == "msan":
                    r.skip("msan", "libc not instrumented")
                else:
                    r.fail("%s: build" % name)
                    if args.verbose:
                        print(out + err)
                continue

            failed = False
            for src in TESTS:
                if not Path(src).exists():
                    continue
                exe = os.path.join(tmp, Path(src).stem)
                cmd = [args.cc, "-std=gnu11", "-g"]
                cmd += flags.split()
                cmd += ["-I" + i for i in INCLUDES]
                cmd += [src, "-L" + tmp, "-lolsrt",
                        "-Wl,-rpath," + tmp]
                cmd += LINK_LIBS + ["-o", exe]
                rc, out, err = run(cmd)
                if rc != 0:
                    r.fail("%s: build %s" % (name, Path(src).stem))
                    for ln in (out + err).splitlines()[:15]:
                        print("      " + ln)
                    failed = True
                    continue
                env = dict(os.environ)
                env.update(env_over)
                rc, out, err = run([exe], env=env, timeout=120)
                if rc != 0:
                    r.fail("%s: run %s" % (name, Path(src).stem))
                    for ln in (out + err).splitlines()[:15]:
                        print("      " + ln)
                    failed = True

            if not failed:
                r.ok(name)

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
