#!/usr/bin/env python3
"""
ci/lint_check.py — run every detected C linter.

Detected backends: clang-tidy, cppcheck, sparse, smatch, splint.

The tool fails if any backend reports an error. Warnings from
clang-tidy are reported but do not fail by default; use --strict to
also fail on warnings.
"""

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

INCLUDES = ["includes", "includes/code", "includes/code/streams",
            "includes/runtime"]

def compile_commands_exists():
    return Path("compile_commands.json").exists() or \
           Path("build/compile_commands.json").exists()

def sources():
    return sorted(str(p) for p in Path("src/code/streams").glob("*.c"))

def run_clang_tidy(reporter, verbose, strict):
    if not has_tool("clang-tidy"):
        return False
    # clang-tidy is only useful if it has a compilation database.
    if not compile_commands_exists():
        reporter.skip("clang-tidy", "no compile_commands.json")
        return True
    bad = 0
    for src in sources():
        rc, out, err = run(["clang-tidy", src, "--", "-std=gnu11"],
                           timeout=120)
        combined = out + err
        if "error:" in combined:
            bad += 1
            if verbose:
                print("    " + src)
        elif strict and "warning:" in combined:
            bad += 1
            if verbose:
                print("    " + src)
    if bad:
        reporter.fail("clang-tidy", "%d file(s)" % bad)
    else:
        reporter.ok("clang-tidy")
    return True

def run_cppcheck(reporter, verbose, strict):
    if not has_tool("cppcheck"):
        return False
    cmd = ["cppcheck", "--enable=warning,style,performance,portability",
           "--error-exitcode=1", "--quiet",
           "--suppress=missingIncludeSystem"]
    cmd += ["-I" + i for i in INCLUDES]
    cmd += sources()
    rc, out, err = run(cmd, timeout=180)
    combined = out + err
    if rc != 0:
        reporter.fail("cppcheck", "see output")
        if verbose:
            print(combined)
    else:
        reporter.ok("cppcheck")
    return True

def run_sparse(reporter, verbose, strict):
    if not has_tool("sparse"):
        return False
    cmd = ["sparse", "-Wall"]
    cmd += ["-I" + i for i in INCLUDES]
    cmd += sources()
    rc, out, err = run(cmd, timeout=120)
    combined = out + err
    if rc != 0:
        reporter.fail("sparse")
        if verbose:
            print(combined)
    else:
        reporter.ok("sparse")
    return True

def run_smatch(reporter, verbose, strict):
    if not has_tool("smatch"):
        return False
    cmd = ["smatch"]
    cmd += ["-I" + i for i in INCLUDES]
    cmd += sources()
    rc, out, err = run(cmd, timeout=180)
    combined = out + err
    if "error:" in combined:
        reporter.fail("smatch")
        if verbose:
            print(combined)
    else:
        reporter.ok("smatch")
    return True

def run_splint(reporter, verbose, strict):
    if not has_tool("splint"):
        return False
    cmd = ["splint", "+quiet"]
    cmd += ["-I" + i for i in INCLUDES]
    cmd += sources()
    rc, out, err = run(cmd, timeout=120)
    if rc != 0:
        reporter.fail("splint")
        if verbose:
            print(out + err)
    else:
        reporter.ok("splint")
    return True

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--strict", action="store_true",
                    help="Treat warnings as failures too.")
    args = ap.parse_args(argv)

    r = Reporter("lint_check", verbose=args.verbose)
    ran = False
    for fn in (run_clang_tidy, run_cppcheck, run_sparse,
               run_smatch, run_splint):
        if fn(r, args.verbose, args.strict):
            ran = True
    if not ran:
        r.skip("all linters", "none installed")
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
