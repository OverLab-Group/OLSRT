#!/usr/bin/env python3
"""
diagnose_cmake_build.py

Part A - commit any pending changes from the previous script (the
         CMake pkg-config fix that made configure succeed).

Part B - run a fresh CMake build and print ONLY the lines that look
         like actual compile or link errors. The previous run buried
         the error inside 395 progress lines; this extracts the real
         problem.

No commit is made by this script until we know what the fix is.

Run from the repository root:
    python3 diagnose_cmake_build.py
"""

import re
import shutil
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
# Part A - commit pending changes
# ---------------------------------------------------------------------

def commit_pending():
    section("A. commit pending changes")

    rc, status, _ = git("status", "--porcelain")
    if not status.strip():
        print("  working tree is clean")
        return

    to_add = []
    for ln in status.splitlines():
        if len(ln) < 4:
            continue
        path = ln[3:]
        if path.startswith(("build/", "bin/")):
            continue
        to_add.append(path)

    if not to_add:
        print("  nothing to stage")
        return

    print("  staging %d path(s):" % len(to_add))
    for p in to_add:
        print("    " + p)

    git("add", "--", *to_add)
    message = (
        "build: fix cmake configure; make cmake_check report errors\n"
        "\n"
        "- CMakeLists.txt: the GENERATE_PKGCONFIG block called\n"
        "  configure_package_config_file() on\n"
        "  cmake/olsrt-config.cmake.in, which did not exist in the\n"
        "  tree. The option is now OFF by default and the block is\n"
        "  guarded by EXISTS.\n"
        "- cmake/olsrt-config.cmake.in: placeholder created.\n"
        "- ci/cmake_check.py: on failure, always print the first 20\n"
        "  lines of stdout+stderr, regardless of --verbose.\n"
    )
    rc, _, err = git("commit", "-m", message)
    if rc != 0:
        print("  commit failed: %s" % err.strip())
    else:
        print("  committed (not pushed)")

# ---------------------------------------------------------------------
# Part B - diagnose the CMake build
# ---------------------------------------------------------------------

ERROR_PATTERNS = [
    re.compile(r"\berror\s*:", re.IGNORECASE),
    re.compile(r"\bError\b"),
    re.compile(r"FAILED:"),
    re.compile(r"undefined reference"),
    re.compile(r"cannot find -l"),
    re.compile(r"No such file or directory"),
    re.compile(r"\bwarning:\s"),
]

PROGRESS_RE = re.compile(r"^\[\s*\d+%\]")

def run_cmake_build():
    section("B. diagnose CMake build")

    build_dir = Path("/tmp/olsrt_cmake_diag")
    if build_dir.exists():
        shutil.rmtree(build_dir)

    print("  running: cmake -S . -B %s" % build_dir)
    proc = subprocess.run(
        ["cmake", "-S", ".", "-B", str(build_dir),
         "-DCMAKE_BUILD_TYPE=Release"],
        capture_output=True, text=True, timeout=180)
    if proc.returncode != 0:
        print("  configure failed (exit %d)" % proc.returncode)
        for ln in (proc.stdout + proc.stderr).splitlines()[-20:]:
            print("    " + ln)
        return False

    print("  running: cmake --build %s" % build_dir)
    proc = subprocess.run(
        ["cmake", "--build", str(build_dir), "-j"],
        capture_output=True, text=True, timeout=900)

    print("  build exit code: %d" % proc.returncode)
    print()

    combined = (proc.stdout or "") + "\n" + (proc.stderr or "")
    lines = combined.splitlines()

    # Filter out progress and object-file success lines.
    interesting = []
    for ln in lines:
        if PROGRESS_RE.match(ln.strip()):
            continue
        if "Building C object" in ln:
            continue
        if "Built target" in ln:
            continue
        if any(p.search(ln) for p in ERROR_PATTERNS):
            interesting.append(ln)

    if interesting:
        print("  ---- real errors / warnings (up to 40) ----")
        for ln in interesting[:40]:
            print("    " + ln)
        if len(interesting) > 40:
            print("    ... and %d more line(s)"
                  % (len(interesting) - 40))
        print("  -------------------------------------------")
    else:
        print("  no error-looking lines found; last 30 lines of output:")
        for ln in lines[-30:]:
            print("    " + ln)

    print()
    print("  full output saved to /tmp/olsrt_cmake_build.log")
    Path("/tmp/olsrt_cmake_build.log").write_text(combined,
                                                  encoding="utf-8")

    # Try to identify which source file first failed.
    failed_files = set()
    for ln in lines:
        m = re.search(r"Building C object[^:]*/(ol_[a-z_]+)\.c\.o",
                      ln)
        if m:
            failed_files.add(m.group(1) + ".c")

    # The build stopped; the last "Building C object" before any
    # "error:" is a strong candidate.
    last_building = None
    for ln in lines:
        if "Building C object" in ln:
            last_building = ln
        if "error:" in ln.lower() and last_building:
            print("  hint: last file being built when the error "
                  "appeared was:")
            print("    " + last_building)
            break

    shutil.rmtree(build_dir, ignore_errors=True)
    return proc.returncode == 0

def main():
    print()
    commit_pending()
    ok = run_cmake_build()

    print()
    print("====================================================")
    if ok:
        print("CMake build: OK")
        print()
        print("  python3 ci/cmake_check.py   # should now pass")
    else:
        print("CMake build: FAILED")
        print()
        print("Send the 'real errors' block printed above, plus the")
        print("contents of /tmp/olsrt_cmake_build.log if it is small")
        print("(under 100 lines).")
    print("====================================================")
    return 0

if __name__ == "__main__":
    sys.exit(main())
