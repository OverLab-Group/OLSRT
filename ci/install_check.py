#!/usr/bin/env python3
"""
ci/install_check.py — verify that the Makefile install target works
with a custom DESTDIR and prefix.

Also checks that a pkg-config file (if shipped) is valid.
"""

import argparse
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    r = Reporter("install_check", verbose=args.verbose)

    if not Path("Makefile").exists():
        r.skip("Makefile", "not found")
        return r.exit_code()

    # The stock Makefile does not have an install target; if it does
    # not, we skip cleanly.
    rc, out, err = run(["make", "-n", "install"], timeout=30)
    if rc != 0 or "No rule to make target" in (out + err):
        r.skip("make install", "target not present")
    else:
        destdir = tempfile.mkdtemp(prefix="olsrt_destdir_")
        try:
            env = dict(os.environ)
            env["DESTDIR"] = destdir
            env["PREFIX"] = "/usr/local"
            rc, out, err = run(
                ["make", "install"], env=env, timeout=300)
            if rc != 0:
                r.fail("make install")
                if args.verbose:
                    print(out + err)
            else:
                installed = list(Path(destdir).rglob("*"))
                r.ok("make install",
                     "%d file(s)" % len([p for p in installed
                                         if p.is_file()]))
        finally:
            shutil.rmtree(destdir, ignore_errors=True)

    # pkg-config file if it exists.
    pkg = list(Path(".").rglob("*.pc"))
    if not pkg:
        r.skip("pkg-config", "no .pc file shipped")
    elif not has_tool("pkg-config"):
        r.skip("pkg-config", "not installed")
    else:
        for pc in pkg:
            rc, out, err = run(
                ["pkg-config", "--validate", str(pc)],
                timeout=30)
            if rc != 0:
                r.fail("pkg-config validate %s" % pc)
            else:
                r.ok("pkg-config validate %s" % pc)

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
