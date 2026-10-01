#!/usr/bin/env python3
"""
ci_fix_and_commit.py

Part A - fix three issues in the newly added CI toolsuite:

  1. version_check.py calls re.search(compiled_pattern, text, flags),
     which Python raises ValueError on. Switch to
     compiled_pattern.search(text).
  2. version_check.py looks for ol_green_threads.h at the repository
     root; the file lives at includes/code/streams/.
  3. Bump OL_GT_VERSION_PATCH and OL_GT_VERSION_STRING in
     ol_green_threads.h to match the runtime version reported by
     ol_globals.h (1.3.2).

Part B - remove the legacy root-level verify.py and
         diagnose_doxygen.py (their functionality is now in ci/), and
         commit the whole ci/ directory plus the deletions as a single
         logical change.

No push.

Run from the repository root:
    python3 ci_fix_and_commit.py
"""

import os
import re
import subprocess
import sys
from pathlib import Path

def git(*args, check=False):
    r = subprocess.run(["git"] + list(args), capture_output=True,
                       text=True, check=check)
    return r.returncode, r.stdout, r.stderr

def section(title):
    print()
    print("== %s ==" % title)

# ---------------------------------------------------------------------
# Part A - fix version_check.py
# ---------------------------------------------------------------------

def fix_version_check():
    section("A. fix version_check.py")

    path = Path("ci/version_check.py")
    if not path.exists():
        print("  ci/version_check.py not found; skipping")
        return

    text = path.read_text(encoding="utf-8")

    # 1. find_in_file must use the compiled pattern's own .search().
    old = (
        'def find_in_file(path, pattern, label, default=None):\n'
        '    p = Path(path)\n'
        '    if not p.exists():\n'
        '        return None\n'
        '    text = p.read_text(encoding="utf-8")\n'
        '    m = re.search(pattern, text, re.MULTILINE)\n'
        '    return m.group(1) if m else default\n'
    )
    new = (
        'def find_in_file(path, pattern, label, default=None):\n'
        '    p = Path(path)\n'
        '    if not p.exists():\n'
        '        return None\n'
        '    text = p.read_text(encoding="utf-8")\n'
        '    # `pattern` is already a compiled regex.\n'
        '    m = pattern.search(text)\n'
        '    return m.group(1) if m else default\n'
    )
    if old in text:
        text = text.replace(old, new, 1)
        print("  OK: find_in_file uses compiled pattern")
    elif "pattern.search(text)" in text:
        print("  already fixed: find_in_file")
    else:
        print("  warning: find_in_file not in expected form")

    # 2. Correct the header path.
    old_paths = [
        '("ol_green_threads.h", re.compile(',
    ]
    new_paths = [
        '("includes/code/streams/ol_green_threads.h", re.compile(',
    ]
    if old_paths[0] in text:
        text = text.replace(old_paths[0], new_paths[0], 1)
        print("  OK: green-threads header path corrected")
    elif new_paths[0] in text:
        print("  already fixed: header path")

    path.write_text(text, encoding="utf-8")

def bump_green_threads_version():
    section("A. bump green-threads version")

    path = Path("includes/code/streams/ol_green_threads.h")
    if not path.exists():
        print("  header not found; skipping")
        return

    text = path.read_text(encoding="utf-8")

    pairs = [
        (r"#define\s+OL_GT_VERSION_PATCH\s+0",
         "#define OL_GT_VERSION_PATCH 2"),
        (r'#define\s+OL_GT_VERSION_STRING\s+"1\.3\.0"',
         '#define OL_GT_VERSION_STRING "1.3.2"'),
    ]
    changed = 0
    for pat, repl in pairs:
        new_text, n = re.subn(pat, repl, text, count=1)
        if n:
            text = new_text
            changed += 1
    if changed:
        path.write_text(text, encoding="utf-8")
        print("  OK: %d version macro(s) updated" % changed)
    else:
        print("  no change (already at 1.3.2?)")

# ---------------------------------------------------------------------
# Part B - cleanup + commit
# ---------------------------------------------------------------------

def remove_legacy():
    section("B. remove legacy root scripts")
    for name in ("verify.py", "diagnose_doxygen.py"):
        p = Path(name)
        if p.exists():
            # Only remove if the ci/ replacement is present.
            replacement = Path("ci") / (
                "verify.py" if name == "verify.py"
                else "doxygen_check.py")
            if replacement.exists():
                p.unlink()
                print("  removed %s" % name)
            else:
                print("  kept %s (no ci/ replacement yet)" % name)
        else:
            print("  %s not present" % name)

def commit_all():
    section("B. commit ci/ toolsuite")

    rc, status, _ = git("status", "--porcelain")
    print("  current status:")
    for ln in status.splitlines():
        print("    " + ln)
    print()

    # Stage everything we touched.
    git("add", "--", "ci/")
    for name in ("verify.py", "diagnose_doxygen.py"):
        if not Path(name).exists():
            git("add", "-A", "--", name)
    git("add", "--",
        "includes/code/streams/ol_green_threads.h")

    rc, staged, _ = git("diff", "--cached", "--name-only")
    if not staged.strip():
        print("  nothing staged")
        return
    print("  staged for commit:")
    for ln in staged.splitlines():
        print("    " + ln)
    print()

    message = (
        "ci: add OLSRT CI toolsuite\n"
        "\n"
        "Adds ci/, a self-contained Python CI toolsuite that runs\n"
        "alongside the repository without modifying source files.\n"
        "\n"
        "Core: verify.py, doxygen_check.py, build_check.py,\n"
        "  warnings_check.py, version_check.py, run_all.py\n"
        "Quality: format_check, lint_check, static_analysis,\n"
        "  include_check, spelling_check, license_check,\n"
        "  complexity_check\n"
        "Security: secrets_check, security_scan\n"
        "Testing: sanitizer_matrix, valgrind_check, coverage_check,\n"
        "  fuzz_check, flaky_check\n"
        "Build: build_matrix, cmake_check, install_check,\n"
        "  header_check, symbol_check, reproducible_check,\n"
        "  docs_build, changelock_check\n"
        "Release: commit_check, olsrt_commiter, benchmark_check\n"
        "\n"
        "Every tool changes its working directory to the repository\n"
        "root, detects the external binaries it needs at runtime, and\n"
        "skips cleanly when a dependency is not installed. No pip\n"
        "packages required.\n"
        "\n"
        "The legacy root-level verify.py and diagnose_doxygen.py are\n"
        "removed; their replacements are ci/verify.py and\n"
        "ci/doxygen_check.py.\n"
        "\n"
        "Also bump OL_GT_VERSION_PATCH and OL_GT_VERSION_STRING in\n"
        "ol_green_threads.h to match the runtime version (1.3.2).\n"
    )
    rc, out, err = git("commit", "-m", message)
    if rc != 0:
        print("  commit failed: %s" % err.strip())
        return
    print("  committed (not pushed)")

def main():
    print()
    fix_version_check()
    bump_green_threads_version()
    remove_legacy()
    commit_all()

    print()
    print("====================================================")
    print("Next:")
    print("  python3 ci/version_check.py")
    print("  python3 ci/run_all.py")
    print("====================================================")
    return 0

if __name__ == "__main__":
    sys.exit(main())
