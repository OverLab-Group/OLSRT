#!/usr/bin/env python3
"""
ci/docs_build.py — build the documentation.

Two backends:

  doxygen  builds the HTML reference under docu/ or the OUTPUT_DIRECTORY
           declared in Doxyfile.
  sphinx   builds the Sphinx docs from source/ if a conf.py exists.

Exit code 0 iff every requested backend succeeds.
"""

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

def doxygen_pass(reporter, verbose):
    if not Path("Doxyfile").exists():
        reporter.skip("doxygen", "no Doxyfile")
        return
    if not has_tool("doxygen"):
        reporter.skip("doxygen", "not installed")
        return
    rc, out, err = run(["doxygen", "Doxyfile"], timeout=300)
    if rc != 0:
        reporter.fail("doxygen")
        if verbose:
            print(out + err)
        return
    # The output directory is declared by OUTPUT_DIRECTORY.
    out_dir = "docu"
    for ln in Path("Doxyfile").read_text().splitlines():
        if ln.strip().startswith("OUTPUT_DIRECTORY"):
            out_dir = ln.split("=", 1)[1].strip().strip('"')
            break
    if Path(out_dir).is_dir():
        reporter.ok("doxygen", "output in %s/" % out_dir)
    else:
        reporter.fail("doxygen", "no output at %s/" % out_dir)

def sphinx_pass(reporter, verbose):
    if not Path("source/conf.py").exists():
        reporter.skip("sphinx", "no source/conf.py")
        return
    if not has_tool("sphinx-build"):
        reporter.skip("sphinx", "sphinx-build not installed")
        return
    out = "docs/sphinx"
    rc, out_log, err = run(
        ["sphinx-build", "-b", "html", "source", out],
        timeout=300)
    if rc != 0:
        reporter.fail("sphinx")
        if verbose:
            print(out_log + err)
    else:
        reporter.ok("sphinx", "output in %s/" % out)

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    r = Reporter("docs_build", verbose=args.verbose)
    doxygen_pass(r, args.verbose)
    sphinx_pass(r, args.verbose)
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
