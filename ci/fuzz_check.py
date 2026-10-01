#!/usr/bin/env python3
"""
ci/fuzz_check.py — short smoke test for the fuzz targets if any exist.

Backends: libFuzzer (via clang -fsanitize=fuzzer), afl-fuzz, honggfuzz.

By default this tool only checks that the fuzz harness builds and runs
for --seconds seconds (default 10). Long campaigns are the caller's
responsibility.
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

def fuzz_targets():
    """Return fuzz harness sources if a fuzz/ directory exists."""
    roots = ["fuzz", "tests/fuzz"]
    for root in roots:
        p = Path(root)
        if p.is_dir():
            return sorted(str(f) for f in p.glob("*.c"))
    return []

def run_libfuzzer(reporter, target, seconds, verbose):
    clang = shutil.which("clang")
    if not clang:
        return False
    lib_sources = sorted(str(p) for p in
                         Path("src/code/streams").glob("*.c"))
    with tempfile.TemporaryDirectory(prefix="olsrt_fuzz_") as tmp:
        exe = os.path.join(tmp, "fuzz_target")
        cmd = [clang, "-std=gnu11", "-fsanitize=fuzzer,address",
               "-g", "-O1"]
        cmd += ["-I" + i for i in INCLUDES]
        cmd += [target]
        cmd += lib_sources
        cmd += ["-o", exe, "-lpthread", "-lrt", "-ldl"]
        rc, out, err = run(cmd)
        if rc != 0:
            reporter.fail("libFuzzer build", Path(target).name)
            if verbose:
                print(out + err)
            return True
        rc, out, err = run([exe, "-max_total_time=%d" % seconds],
                           timeout=seconds + 30)
        if rc != 0:
            reporter.fail("libFuzzer run", Path(target).name)
            if verbose:
                print(out + err)
        else:
            reporter.ok("libFuzzer run", Path(target).name)
    return True

def run_afl(reporter, target, seconds, verbose):
    if not has_tool("afl-fuzz"):
        return False
    # Building an AFL target requires afl-clang-fast; skip if absent.
    cc = shutil.which("afl-clang-fast")
    if not cc:
        reporter.skip("afl-fuzz", "afl-clang-fast not installed")
        return True
    with tempfile.TemporaryDirectory(prefix="olsrt_afl_") as tmp:
        exe = os.path.join(tmp, "target")
        cmd = [cc, "-std=gnu11", "-g", "-O1"]
        cmd += ["-I" + i for i in INCLUDES]
        cmd += [target]
        cmd += sorted(str(p) for p in
                      Path("src/code/streams").glob("*.c"))
        cmd += ["-o", exe, "-lpthread", "-lrt", "-ldl"]
        rc, out, err = run(cmd)
        if rc != 0:
            reporter.fail("afl build")
            return True
        reporter.info("afl target built; "
                      "run manually for a full campaign")
        reporter.ok("afl build", Path(target).name)
    return True

def run_honggfuzz(reporter, target, seconds, verbose):
    if not has_tool("honggfuzz"):
        return False
    reporter.skip("honggfuzz", "harness not integrated")
    return True

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--seconds", type=int, default=10)
    args = ap.parse_args(argv)

    r = Reporter("fuzz_check", verbose=args.verbose)
    targets = fuzz_targets()
    if not targets:
        r.skip("fuzz targets", "no fuzz/ directory")
        return r.exit_code()

    for target in targets:
        for fn in (run_libfuzzer, run_afl, run_honggfuzz):
            fn(r, target, args.seconds, args.verbose)

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
