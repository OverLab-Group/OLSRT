#!/usr/bin/env python3
"""
ci/_common.py — shared helpers for the OLSRT CI toolsuite.

This module is not a tool itself. Every other script in `ci/` imports
it. Keep the public surface small: `chdir_to_root`, `has_tool`,
`which`, `run`, `Reporter`, `read_version`, and a few helpers.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------------
# Filesystem
# ---------------------------------------------------------------------

def chdir_to_root():
    """Change CWD to the repository root regardless of where the
    caller was invoked from."""
    os.chdir(REPO_ROOT)

# ---------------------------------------------------------------------
# External tool detection
# ---------------------------------------------------------------------

def has_tool(name):
    """True if an executable named `name` is on PATH."""
    return shutil.which(name) is not None

def which(name):
    """Absolute path to `name`, or None."""
    return shutil.which(name)

def first_available(*names):
    """Return (name, path) for the first executable found, else
    (None, None)."""
    for n in names:
        p = shutil.which(n)
        if p:
            return n, p
    return None, None

# ---------------------------------------------------------------------
# Subprocess wrapper
# ---------------------------------------------------------------------

def run(cmd, cwd=None, env=None, timeout=None, capture=True):
    """Run a command. Returns (returncode, stdout, stderr).

    `cmd` may be a string (split on whitespace, no shell) or a list.
    If `capture` is False, output is inherited instead of captured.
    """
    if isinstance(cmd, str):
        cmd = cmd.split()
    try:
        if capture:
            r = subprocess.run(
                cmd, cwd=cwd, env=env, timeout=timeout,
                capture_output=True, text=True,
            )
            return r.returncode, r.stdout or "", r.stderr or ""
        else:
            r = subprocess.run(
                cmd, cwd=cwd, env=env, timeout=timeout,
            )
            return r.returncode, "", ""
    except FileNotFoundError as exc:
        return 127, "", "not found: %s" % exc
    except subprocess.TimeoutExpired:
        return 124, "", "timeout after %s s" % timeout

# ---------------------------------------------------------------------
# Version helpers
# ---------------------------------------------------------------------

def read_version():
    """Parse the version string from includes/runtime/ol_globals.h.

    Returns "MAJOR.MINOR.PATCH" or None if the file or the macros are
    missing.
    """
    path = REPO_ROOT / "includes" / "runtime" / "ol_globals.h"
    if not path.exists():
        return None
    text = path.read_text(encoding="utf-8")
    import re
    major = re.search(r"#define\s+OL_VERSION_MAJOR\s+(\d+)", text)
    minor = re.search(r"#define\s+OL_VERSION_MINOR\s+(\d+)", text)
    patch = re.search(r"#define\s+OL_VERSION_PATCH\s+(\d+)", text)
    if not (major and minor and patch):
        return None
    return "%s.%s.%s" % (major.group(1), minor.group(1), patch.group(1))

# ---------------------------------------------------------------------
# Reporter
# ---------------------------------------------------------------------

class Reporter:
    """Accumulate pass / fail / skip results and print a summary.

    Usage:

        r = Reporter("tool name")
        r.ok("check A")
        r.fail("check B", "reason")
        r.skip("check C", "tool xyz not installed")
        sys.exit(r.exit_code())
    """

    def __init__(self, title, verbose=False):
        self.title = title
        self.verbose = verbose
        self.passed = 0
        self.failed = 0
        self.skipped = 0
        self._failures = []
        print()
        print("== %s ==" % title)
        print()

    def ok(self, name, detail=""):
        self.passed += 1
        line = "  [OK]   %s" % name
        if detail:
            line += "  (%s)" % detail
        print(line)

    def fail(self, name, detail=""):
        self.failed += 1
        self._failures.append(name)
        line = "  [FAIL] %s" % name
        if detail:
            line += "  (%s)" % detail
        print(line)

    def skip(self, name, reason=""):
        self.skipped += 1
        line = "  [SKIP] %s" % name
        if reason:
            line += "  (%s)" % reason
        print(line)

    def info(self, message):
        print("  ... %s" % message)

    def exit_code(self):
        print()
        print("  Summary: %d passed, %d failed, %d skipped"
              % (self.passed, self.failed, self.skipped))
        if self._failures:
            print()
            print("  Failed checks:")
            for name in self._failures:
                print("    - %s" % name)
        print()
        return 1 if self.failed else 0

# ---------------------------------------------------------------------
# Standard flag handling
# ---------------------------------------------------------------------

def add_common_flags(parser):
    """Add the flags every tool should accept."""
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Print full subprocess output.")
    parser.add_argument("--list", action="store_true",
                        help="List the checks this tool would run and exit.")
    return parser
