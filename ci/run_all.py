#!/usr/bin/env python3
"""
ci/run_all.py — run every applicable CI tool in order.

By default the runner stops on the first failure. Pass --keep-going to
run everything and report a final aggregate. Pass --only to restrict
the run to a subset.

Each tool is a separate Python script in this directory. The runner
does not import them; it forks them with the current interpreter so
that a crash in one tool does not take down the runner.
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import chdir_to_root

# Order matters: cheap static checks first, expensive dynamic checks
# later, informational-only tools last.
TOOLS = [
    "version_check.py",
    "warnings_check.py",
    "format_check.py",
    "lint_check.py",
    "spelling_check.py",
    "license_check.py",
    "secrets_check.py",
    "doxygen_check.py",
    "header_check.py",
    "build_check.py",
    "cmake_check.py",
    "install_check.py",
    "verify.py",
    "sanitizer_matrix.py",
    "valgrind_check.py",
    "coverage_check.py",
    "static_analysis.py",
    "security_scan.py",
    "fuzz_check.py",
    "flaky_check.py",
    "commit_check.py",
    "build_matrix.py",
    "reproducible_check.py",
]

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", default=None,
                    help="Comma-separated list of tool filenames.")
    ap.add_argument("--keep-going", action="store_true",
                    help="Do not stop on the first failure.")
    args = ap.parse_args(argv)

    tools = list(TOOLS)
    if args.only:
        wanted = {t.strip() for t in args.only.split(",") if t.strip()}
        tools = [t for t in tools if t in wanted]

    if args.list:
        for t in tools:
            print(t)
        return 0

    ci_dir = Path(__file__).resolve().parent
    results = []  # list of (tool, rc, seconds)

    for name in tools:
        path = ci_dir / name
        if not path.exists():
            print("  [SKIP] %s (file missing)" % name)
            results.append((name, None, 0.0))
            continue

        print()
        print(">>> %s" % name)
        t0 = time.monotonic()
        cmd = [sys.executable, str(path)]
        if args.verbose:
            cmd.append("--verbose")
        rc = subprocess.run(cmd).returncode
        dt = time.monotonic() - t0
        results.append((name, rc, dt))

        if rc != 0 and not args.keep_going:
            print()
            print("  Stopping on first failure (use --keep-going to "
                  "continue).")
            break

    # Summary
    print()
    print("=" * 60)
    print("run_all summary")
    print("=" * 60)
    for name, rc, dt in results:
        if rc is None:
            status = "SKIP"
        elif rc == 0:
            status = "PASS"
        else:
            status = "FAIL"
        print("  %-24s %-6s %6.2fs" % (name, status, dt))

    failed = sum(1 for _, rc, _ in results if rc not in (0, None))
    print()
    print("  %d tool(s) run, %d failure(s)" % (len(results), failed))
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
