#!/usr/bin/env python3
"""
ci/static_analysis.py — run every detected static analyzer.

Detected backends: clang --analyze, infer, semgrep, codeql,
pvs-studio-analyzer.
"""

import argparse
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

INCLUDES = ["includes", "includes/code", "includes/code/streams",
            "includes/runtime"]

def sources():
    return sorted(str(p) for p in Path("src/code/streams").glob("*.c"))

def run_clang_analyze(reporter, verbose):
    cc = shutil.which("clang") or shutil.which("clang-18") or \
         shutil.which("clang-17")
    if not cc:
        return False
    bad = 0
    for src in sources():
        cmd = [cc, "--analyze", "-std=gnu11"]
        cmd += ["-I" + i for i in INCLUDES]
        cmd += [src, "-o", os.devnull] if False else [src]
        rc, out, err = run(cmd, timeout=120)
        if rc != 0 or "warning:" in (out + err):
            bad += 1
            if verbose:
                print("    " + src)
    if bad:
        reporter.fail("clang --analyze", "%d file(s)" % bad)
    else:
        reporter.ok("clang --analyze")
    return True

def run_infer(reporter, verbose):
    if not has_tool("infer"):
        return False
    cmd = ["infer", "--", "make", "-C", ".", "clean"]
    # Without a full build integration, run infer against make.
    cmd = ["infer", "--", "make", "TARGET=linux", "ARCH=x86_64"]
    rc, out, err = run(cmd, timeout=600)
    if rc != 0 or "Found" not in out + err:
        reporter.fail("infer")
        if verbose:
            print(out + err)
    else:
        reporter.ok("infer")
    return True

def run_semgrep(reporter, verbose):
    if not has_tool("semgrep"):
        return False
    cmd = ["semgrep", "--config=auto", "--error", "--quiet",
           "src/", "includes/"]
    rc, out, err = run(cmd, timeout=600)
    if rc != 0:
        reporter.fail("semgrep")
        if verbose:
            print(out + err)
    else:
        reporter.ok("semgrep")
    return True

def run_codeql(reporter, verbose):
    if not has_tool("codeql"):
        return False
    with tempfile.TemporaryDirectory(prefix="olsrt_codeql_") as db:
        rc, _, err = run(["codeql", "database", "create", db,
                          "--language=c-cpp", "--source-root=."],
                         timeout=900)
        if rc != 0:
            reporter.fail("codeql database")
            if verbose:
                print(err)
            return True
        rc, out, err = run(["codeql", "database", "analyze", db,
                            "codeql/cpp-queries",
                            "--format=sarif-latest",
                            "--output=/dev/null"],
                           timeout=1800)
        if rc != 0:
            reporter.fail("codeql analyze")
            if verbose:
                print(out + err)
        else:
            reporter.ok("codeql")
    return True

def run_pvs(reporter, verbose):
    if not has_tool("pvs-studio-analyzer"):
        return False
    cmd = ["pvs-studio-analyzer", "trace",
           "--file=PVS-Studio.log",
           "make", "TARGET=linux", "ARCH=x86_64"]
    rc, _, err = run(cmd, timeout=900)
    if rc != 0:
        reporter.fail("pvs-studio-analyzer")
        if verbose:
            print(err)
    else:
        reporter.ok("pvs-studio-analyzer")
    return True

import os  # noqa: E402  (needed by run_clang_analyze)

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    r = Reporter("static_analysis", verbose=args.verbose)
    ran = False
    for fn in (run_clang_analyze, run_infer, run_semgrep,
               run_codeql, run_pvs):
        if fn(r, args.verbose):
            ran = True
    if not ran:
        r.skip("all analyzers", "none installed")
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
