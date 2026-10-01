#!/usr/bin/env python3
"""
ci/format_check.py — verify source formatting with the first
available formatter among clang-format, uncrustify, astyle.

Exit code 0 iff every detected formatter reports no diff, or iff no
formatter is installed (which produces a skip).
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

SOURCE_GLOBS = ["src/**/*.c", "src/**/*.h", "includes/**/*.h"]

def collect_sources():
    files = []
    for pattern in SOURCE_GLOBS:
        files.extend(sorted(str(p) for p in Path(".").glob(pattern)))
    return files

def check_clang_format(reporter, sources, verbose):
    tool = shutil.which("clang-format")
    if not tool:
        return False
    cfg = None
    for name in (".clang-format", "_clang-format"):
        if Path(name).exists():
            cfg = name
            break
    rc, _, _ = run([tool, "--version"])
    bad = 0
    for src in sources:
        cmd = [tool, "--dry-run", "-Werror"]
        if cfg:
            cmd += ["-style=file:" + cfg]
        cmd += [src]
        rc, _, err = run(cmd)
        if rc != 0:
            bad += 1
            if verbose:
                print("    " + src)
    if bad:
        reporter.fail("clang-format", "%d file(s) differ" % bad)
    else:
        reporter.ok("clang-format", "%d file(s)" % len(sources))
    return True

def check_uncrustify(reporter, sources, verbose):
    tool = shutil.which("uncrustify")
    if not tool:
        return False
    cfg = None
    for name in ("uncrustify.cfg", ".uncrustify.cfg"):
        if Path(name).exists():
            cfg = name
            break
    if not cfg:
        reporter.skip("uncrustify", "no config file")
        return True
    bad = 0
    for src in sources:
        cmd = [tool, "-c", cfg, "-l", "C", "-f", src, "--check"]
        rc, _, _ = run(cmd)
        if rc != 0:
            bad += 1
            if verbose:
                print("    " + src)
    if bad:
        reporter.fail("uncrustify", "%d file(s) differ" % bad)
    else:
        reporter.ok("uncrustify", "%d file(s)" % len(sources))
    return True

def check_astyle(reporter, sources, verbose):
    tool = shutil.which("astyle")
    if not tool:
        return False
    opts = ["--options=.astylerc"] if Path(".astylerc").exists() else []
    bad = 0
    for src in sources:
        cmd = [tool] + opts + ["--dry-run", "--suffix=none", src]
        rc, out, _ = run(cmd)
        if rc != 0 or "Formatted" in out:
            bad += 1
            if verbose:
                print("    " + src)
    if bad:
        reporter.fail("astyle", "%d file(s) differ" % bad)
    else:
        reporter.ok("astyle", "%d file(s)" % len(sources))
    return True

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args(argv)

    sources = collect_sources()
    if args.list:
        for s in sources:
            print(s)
        return 0

    r = Reporter("format_check", verbose=args.verbose)
    r.info("source files: %d" % len(sources))
    print()

    ran = False
    for fn in (check_clang_format, check_uncrustify, check_astyle):
        if fn(r, sources, args.verbose):
            ran = True

    if not ran:
        r.skip("all formatters", "none of clang-format, uncrustify, "
                                 "astyle installed")
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
