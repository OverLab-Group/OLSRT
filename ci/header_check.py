#!/usr/bin/env python3
"""
ci/header_check.py — self-containment and include-guard hygiene for
every header under includes/.

Two checks per header:

1. Include guard. The first non-comment, non-blank directive must be
   `#ifndef NAME` / `#define NAME` with the same NAME, and the file
   must end with `#endif` referencing NAME.

2. Self-containment. The header compiles when included on its own.
"""

import argparse
import os
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, run

INCLUDE_DIRS = ["includes", "includes/code", "includes/code/streams",
                "includes/runtime"]

def headers():
    return sorted(str(p) for p in Path("includes").rglob("*.h"))

GUARD_RE = re.compile(
    r"^\s*#\s*ifndef\s+([A-Z_][A-Z0-9_]*)\s*\n"
    r"\s*#\s*define\s+(\1)\b",
    re.MULTILINE,
)

def check_guard(reporter, hdr):
    text = Path(hdr).read_text(encoding="utf-8")
    m = GUARD_RE.search(text)
    if not m:
        reporter.fail("guard %s" % hdr, "no #ifndef/#define pair")
        return
    name = m.group(1)
    if not re.search(r"#\s*endif[^\n]*" + re.escape(name), text):
        reporter.fail("guard %s" % hdr, "no matching #endif for %s" % name)
        return
    reporter.ok("guard %s" % hdr)

def check_self_contained(reporter, hdr, cc, verbose):
    with tempfile.TemporaryDirectory(prefix="olsrt_hdr_") as tmp:
        src = os.path.join(tmp, "probe.c")
        with open(src, "w") as f:
            f.write('#include "%s"\nint main(void){return 0;}\n' % hdr)
        cmd = [cc, "-std=gnu11", "-fsyntax-only"]
        cmd += ["-I" + i for i in INCLUDE_DIRS]
        cmd += [src]
        rc, out, err = run(cmd)
        if rc != 0:
            reporter.fail("self-contained %s" % hdr)
            if verbose:
                print(out + err)
        else:
            reporter.ok("self-contained %s" % hdr)

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args(argv)

    r = Reporter("header_check", verbose=args.verbose)
    for hdr in headers():
        check_guard(r, hdr)
        check_self_contained(r, hdr, args.cc, args.verbose)
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
