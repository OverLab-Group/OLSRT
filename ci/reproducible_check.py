#!/usr/bin/env python3
"""
ci/reproducible_check.py — verify that two builds with the same
SOURCE_DATE_EPOCH produce byte-identical shared objects.

This is a prerequisite for reproducible releases. Differences can come
from embedded timestamps, compiler version strings, or path names.
"""

import argparse
import hashlib
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, run, LINK_LIBS

INCLUDES = ["includes", "includes/code", "includes/code/streams",
            "includes/runtime"]

EPOCH = "1700000000"  # 2023-11-14 UTC

def build(tmp, cc):
    lib = os.path.join(tmp, "libolsrt.so")
    sources = sorted(str(p) for p in
                     Path("src/code/streams").glob("*.c"))
    cmd = [cc, "-std=gnu11", "-fPIC", "-shared", "-O2",
           "-ffile-prefix-map=" + str(Path.cwd()) + "=."]
    cmd += ["-I" + i for i in INCLUDES]
    cmd += sources
    cmd += ["-o", lib] + LINK_LIBS
    env = dict(os.environ)
    env["SOURCE_DATE_EPOCH"] = EPOCH
    rc, out, err = run(cmd, env=env)
    return lib, rc, out + err

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args(argv)

    r = Reporter("reproducible_check", verbose=args.verbose)
    r.info("SOURCE_DATE_EPOCH=%s" % EPOCH)
    print()

    digests = []
    for i in (1, 2):
        with tempfile.TemporaryDirectory(
                prefix="olsrt_rep_%d_" % i) as tmp:
            lib, rc, log = build(tmp, args.cc)
            if rc != 0:
                r.fail("build %d" % i)
                if args.verbose:
                    print(log)
                return r.exit_code()
            digests.append(sha256(lib))

    r.info("digest 1: %s" % digests[0][:16])
    r.info("digest 2: %s" % digests[1][:16])
    if digests[0] == digests[1]:
        r.ok("builds are byte-identical")
    else:
        r.fail("builds differ")

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
