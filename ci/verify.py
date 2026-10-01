#!/usr/bin/env python3
"""
ci/verify.py — build the library under ASan+UBSan and TSan, then run
the test suite in each configuration.

This replaces the previous root-level verify.py. It compiles:

  - the library from src/code/streams/*.c
  - every test in tests/ (recursively, matching *.c that define main)
  - the small harnesses under tests/security/

For each sanitizer combination it runs the full suite and reports
failures. LSan is enabled for the ASan pass.

Exit code 0 iff every configuration and every test passed.
"""

import argparse
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

LIB_SRC_GLOB = "src/code/streams/*.c"
TEST_GLOBS = ["tests/*.c", "tests/security/*.c"]
INCLUDE_DIRS = [
    "includes",
    "includes/code",
    "includes/code/streams",
    "includes/runtime",
]

CONFIGS = [
    ("asan+ubsan+lsan",
     "-fsanitize=address,undefined -fsanitize-address-use-after-scope "
     "-fno-omit-frame-pointer -g -O1",
     {"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=0",
      "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"}),
    ("tsan",
     "-fsanitize=thread -fno-omit-frame-pointer -g -O1",
     {"TSAN_OPTIONS": "halt_on_error=1:second_deadlock_stack=1"}),
]

def collect_sources():
    """Return the list of .c files under src/code/streams and the
    list of test programs (files with a `main` symbol)."""
    lib_sources = sorted(str(p) for p in Path(".").glob(LIB_SRC_GLOB))
    test_sources = []
    for pattern in TEST_GLOBS:
        for p in sorted(Path(".").glob(pattern)):
            test_sources.append(str(p))
    return lib_sources, test_sources

def build_library(tmpdir, cc, flags, lib_sources):
    """Compile all library sources into a single shared object."""
    out = os.path.join(tmpdir, "libolsrt.so")
    cmd = [cc, "-std=gnu11", "-fPIC", "-shared", "-O1"]
    cmd += flags.split()
    cmd += ["-I" + d for d in INCLUDE_DIRS]
    cmd += lib_sources
    cmd += ["-o", out, "-lpthread", "-lrt", "-ldl"]
    rc, stdout, stderr = run(cmd)
    return rc, out, stdout + stderr

def build_test(tmpdir, cc, flags, src, lib):
    """Compile one test source against the sanitized library."""
    out = os.path.join(tmpdir, Path(src).stem)
    cmd = [cc, "-std=gnu11", "-O1", "-g"]
    cmd += flags.split()
    cmd += ["-I" + d for d in INCLUDE_DIRS]
    cmd += [src, "-L" + tmpdir, "-lolsrt",
            "-Wl,-rpath," + tmpdir, "-lpthread", "-lrt", "-ldl",
            "-o", out]
    rc, stdout, stderr = run(cmd)
    return rc, out, stdout + stderr

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--config", action="append",
                    help="Run only the named config. Repeatable.")
    args = ap.parse_args(argv)

    cc = os.environ.get("CC", "cc")
    if not has_tool(cc):
        print("error: C compiler %r not found" % cc, file=sys.stderr)
        return 2

    lib_sources, test_sources = collect_sources()

    if args.list:
        print("library sources (%d):" % len(lib_sources))
        for s in lib_sources:
            print("  " + s)
        print("tests (%d):" % len(test_sources))
        for s in test_sources:
            print("  " + s)
        return 0

    wanted = set(args.config) if args.config else None
    configs = [c for c in CONFIGS if not wanted or c[0] in wanted]

    r = Reporter("verify")
    r.info("compiler: %s" % cc)
    r.info("library sources: %d" % len(lib_sources))
    r.info("test programs: %d" % len(test_sources))
    print()

    for name, flags, env_overrides in configs:
        print("  --- config: %s ---" % name)
        with tempfile.TemporaryDirectory(prefix="olsrt_verify_") as tmp:
            rc, lib, log = build_library(tmp, cc, flags, lib_sources)
            if rc != 0:
                r.fail("%s: library build" % name, "see output above")
                if args.verbose:
                    print(log)
                continue
            r.ok("%s: library build" % name)

            for src in test_sources:
                rc, exe, log = build_test(tmp, cc, flags, src, lib)
                if rc != 0:
                    r.fail("%s: build %s" % (name, src))
                    if args.verbose:
                        print(log)
                    continue

                env = dict(os.environ)
                env.update(env_overrides)
                rc, out, err = run([exe], env=env, timeout=120)
                combined = out + err
                if rc != 0:
                    r.fail("%s: run %s" % (name, Path(src).stem),
                           "exit %d" % rc)
                    if args.verbose:
                        print(combined)
                else:
                    r.ok("%s: run %s" % (name, Path(src).stem))

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
