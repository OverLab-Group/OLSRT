#!/usr/bin/env python3
"""
ci/cmake_check.py — configure, build, install and uninstall via CMake.

Runs the full round trip in a temporary prefix so that nothing on the
host system is touched:

  cmake -S . -B build/ci_cmake
  cmake --build build/ci_cmake
  cmake --install build/ci_cmake --prefix <tmp>
  verify the installed files exist
  rm -rf the temporary tree

Exit code 0 iff every step succeeds.
"""

import argparse
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

def show_failure(out, err, max_lines=20):
    """Print the first max_lines of out+err."""
    text = (out or "") + (err or "")
    lines = text.splitlines()
    for ln in lines[:max_lines]:
        print("      " + ln)
    if len(lines) > max_lines:
        print("      ... and %d more line(s)"
              % (len(lines) - max_lines))


def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--keep", action="store_true",
                    help="Do not delete the build directory.")
    args = ap.parse_args(argv)

    r = Reporter("cmake_check", verbose=args.verbose)

    if not Path("CMakeLists.txt").exists():
        r.skip("cmake", "no CMakeLists.txt")
        return r.exit_code()
    if not has_tool("cmake"):
        r.skip("cmake", "not installed")
        return r.exit_code()

    build_dir = Path("build/ci_cmake")
    if build_dir.exists():
        shutil.rmtree(build_dir)

    prefix = Path(tempfile.mkdtemp(prefix="olsrt_cmake_prefix_"))

    try:
        rc, out, err = run(
            ["cmake", "-S", ".", "-B", str(build_dir),
             "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_INSTALL_PREFIX=" + str(prefix)],
            timeout=180)
        if rc != 0:
            r.fail("configure")
            show_failure(out, err)
            return r.exit_code()
        r.ok("configure")

        rc, out, err = run(
            ["cmake", "--build", str(build_dir), "-j"],
            timeout=600)
        if rc != 0:
            r.fail("build")
            show_failure(out, err)
            return r.exit_code()
        r.ok("build")

        rc, out, err = run(
            ["cmake", "--install", str(build_dir)],
            timeout=180)
        if rc != 0:
            r.fail("install")
            show_failure(out, err)
            return r.exit_code()
        r.ok("install")

        # Verify that headers landed.
        headers = list(prefix.rglob("*.h"))
        if headers:
            r.ok("headers installed", "%d file(s)" % len(headers))
        else:
            r.fail("headers installed", "none found under prefix")

    finally:
        if not args.keep:
            if build_dir.exists():
                shutil.rmtree(build_dir)
        shutil.rmtree(prefix, ignore_errors=True)

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
