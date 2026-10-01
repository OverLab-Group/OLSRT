#!/usr/bin/env python3
"""
ci/changelock_check.py — verify that CHANGELOG.md follows Keep a
Changelog.

Checks performed:

  - The file exists and starts with a title and a "Keep a Changelog"
    reference.
  - Every released version header uses the form
    `## [x.y.z] - YYYY-MM-DD` or `## [Unreleased]`.
  - Version headers appear in descending order.
  - Each released version has at least one of the standard section
    headings (Added / Changed / Fixed / Removed / Deprecated /
    Security).
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root

CHANGELOG = "docs/CHANGELOG.md"

VERSION_RE = re.compile(
    r"^##\s+\[([^\]]+)\](?:\s*-\s*(\d{4}-\d{2}-\d{2}))?\s*$",
    re.MULTILINE)

SECTION_RE = re.compile(
    r"^###\s+(Added|Changed|Fixed|Removed|Deprecated|Security|"
    r"Known issues|Verification)\s*$",
    re.MULTILINE)

def parse_version(s):
    m = re.match(r"^(\d+)\.(\d+)\.(\d+)$", s)
    if not m:
        return None
    return tuple(int(x) for x in m.groups())

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    r = Reporter("changelock_check", verbose=args.verbose)

    p = Path(CHANGELOG)
    if not p.exists():
        r.fail("file exists", CHANGELOG)
        return r.exit_code()
    r.ok("file exists", CHANGELOG)

    text = p.read_text(encoding="utf-8")

    if "Keep a Changelog" in text:
        r.ok("references Keep a Changelog")
    else:
        r.fail("references Keep a Changelog")

    # Collect version headers.
    versions = []
    for m in VERSION_RE.finditer(text):
        versions.append((m.group(1), m.group(2)))

    if not versions:
        r.fail("version headers", "none found")
        return r.exit_code()
    r.ok("version headers", "%d found" % len(versions))

    # Descending order for numeric versions.
    numeric = [(parse_version(v), v) for v, _ in versions
               if parse_version(v) is not None]
    for (prev, pname), (cur, cname) in zip(numeric, numeric[1:]):
        if cur >= prev:
            r.fail("version order", "%s before %s" % (pname, cname))
            break
    else:
        r.ok("version order")

    # Each released version should have at least one section.
    sections = SECTION_RE.findall(text)
    if not sections:
        r.fail("section headings", "none found")
    else:
        r.ok("section headings", "%d found" % len(sections))

    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
