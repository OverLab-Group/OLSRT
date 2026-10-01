#!/usr/bin/env python3
"""
ci/license_check.py — license header compliance.

Backends: reuse, scancode, licensecheck.

The project ships under Apache-2.0. Every source file must carry an
SPDX header, and every license file must be present.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

REQUIRED_LICENSE_FILES = ["LICENSE"]

def run_reuse(reporter, verbose):
    if not has_tool("reuse"):
        return False
    rc, out, err = run(["reuse", "lint"], timeout=180)
    if rc != 0:
        reporter.fail("reuse lint")
        if verbose:
            print(out + err)
    else:
        reporter.ok("reuse lint")
    return True

def run_scancode(reporter, verbose):
    if not has_tool("scancode"):
        return False
    rc, out, err = run(
        ["scancode", "--license", "--quiet",
         "--json-pp", "/tmp/scancode.json",
         "src", "includes", "tests"],
        timeout=600)
    if rc != 0:
        reporter.fail("scancode")
        if verbose:
            print(out + err)
    else:
        reporter.ok("scancode")
    return True

def run_licensecheck(reporter, verbose):
    if not has_tool("licensecheck"):
        return False
    rc, out, err = run(["licensecheck", "-r", "src", "includes"],
                       timeout=120)
    if rc != 0:
        reporter.fail("licensecheck")
        if verbose:
            print(out + err)
    else:
        reporter.ok("licensecheck")
    return True

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    r = Reporter("license_check", verbose=args.verbose)

    # Verify required files exist regardless of backends.
    for path in REQUIRED_LICENSE_FILES:
        if Path(path).exists():
            r.ok("file %s" % path)
        else:
            r.fail("file %s" % path, "missing")

    ran = False
    for fn in (run_reuse, run_scancode, run_licensecheck):
        if fn(r, args.verbose):
            ran = True
    if not ran:
        r.skip("license backends", "none installed")
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
