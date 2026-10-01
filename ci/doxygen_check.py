#!/usr/bin/env python3
"""
ci/doxygen_check.py — strict Doxygen coverage check.

Two passes:

1. Structural. For every function prototype declared in a public header
   under includes/code/streams/, locate its definition in the matching
   source file and inspect the comment block immediately above it.
   Classification:
       MISSING  no @brief
       PARTIAL  has @brief but is missing @param / @return (as applicable)
       RICH     @brief plus every applicable tag
   The tool fails if any public prototype is MISSING or PARTIAL.

2. Doxygen build. Run `doxygen Doxyfile` and count warnings. Any
   warning is reported but does not fail the check by itself, because
   the structural pass is the source of truth.
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

HEADER_DIRS = ["includes/code/streams"]

PROTOTYPE_PATTERN = re.compile(
    r"^[a-zA-Z_][\w \t\*]*?\b(?P<name>ol_\w+)\s*"
    r"\((?P<params>[^;{]*)\)\s*;",
    re.MULTILINE,
)

DEFINITION_PATTERN = re.compile(
    r"^[a-zA-Z_][\w \t\*]*?\b(?P<name>ol_\w+)\s*"
    r"\((?P<params>[^;{]*)\)\s*\{",
    re.MULTILINE,
)

def find_comment_above(text, pos):
    """Return (start, end) of the Doxygen block immediately above
    `pos`, or None."""
    p = pos
    while p > 0 and text[p - 1] in " \t\r\n":
        p -= 1
    if p < 2 or text[p - 2:p] != "*/":
        return None
    end = p
    idx = text.rfind("/**", 0, end)
    if idx < 0:
        return None
    if "*/" in text[idx + 3:end - 2]:
        return None
    return idx, end

def param_names(param_string):
    """Extract parameter names from a C parameter list."""
    if not param_string.strip() or param_string.strip() == "void":
        return []
    names = []
    for chunk in param_string.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        # Remove array suffixes and pointers, take the last identifier.
        m = re.findall(r"[A-Za-z_]\w*", chunk)
        if m:
            names.append(m[-1])
    return names

def classify(block, params, has_return):
    """Return 'MISSING' | 'PARTIAL' | 'RICH'."""
    if block is None or "@brief" not in block:
        return "MISSING"
    missing = []
    for p in params:
        if not re.search(r"@param\s+" + re.escape(p) + r"\b", block):
            missing.append(p)
    if has_return and not re.search(r"@return\b", block):
        missing.append("(return)")
    return "RICH" if not missing else "PARTIAL"

def collect_declarations():
    """Return a dict name -> (header, params, has_return)."""
    decls = {}
    for hdir in HEADER_DIRS:
        for hpath in sorted(Path(hdir).glob("*.h")):
            text = hpath.read_text(encoding="utf-8")
            for m in PROTOTYPE_PATTERN.finditer(text):
                name = m.group("name")
                if name in decls:
                    continue
                params = param_names(m.group("params"))
                # Heuristic: a return tag is expected unless the
                # prototype starts with `void`.
                line = text[m.start():text.find("\n", m.start())]
                head = line.split(name, 1)[0]
                has_return = "void" not in head
                decls[name] = (str(hpath), params, has_return)
    return decls

def collect_definitions():
    """Return a dict name -> (source_path, text, position)."""
    defs = {}
    for src in sorted(Path("src/code/streams").glob("*.c")):
        text = src.read_text(encoding="utf-8")
        for m in DEFINITION_PATTERN.finditer(text):
            name = m.group("name")
            if name in defs:
                continue
            defs[name] = (str(src), text, m.start())
    return defs

def structural_pass(reporter, verbose):
    decls = collect_declarations()
    defs = collect_definitions()
    reporter.info("public prototypes: %d" % len(decls))
    reporter.info("definitions found: %d" % len(defs))
    print()

    for name, (hdr, params, has_return) in sorted(decls.items()):
        if name not in defs:
            reporter.fail("%s (defined in %s)" % (name, hdr),
                          "no definition found")
            continue
        src, text, pos = defs[name]
        block_loc = find_comment_above(text, pos)
        block = text[block_loc[0]:block_loc[1]] if block_loc else None
        state = classify(block, params, has_return)
        if state == "RICH":
            reporter.ok(name)
        elif state == "PARTIAL":
            reporter.fail(name, "PARTIAL (missing @param / @return)")
        else:
            reporter.fail(name, "MISSING @brief")

def doxygen_pass(reporter, verbose):
    if not Path("Doxyfile").exists():
        reporter.skip("doxygen build", "no Doxyfile")
        return
    if not has_tool("doxygen"):
        reporter.skip("doxygen build", "doxygen not installed")
        return
    rc, out, err = run(["doxygen", "Doxyfile"], timeout=180)
    combined = out + err
    warnings = [ln for ln in combined.splitlines()
                if ": warning:" in ln]
    errors = [ln for ln in combined.splitlines()
              if ": error:" in ln]
    if errors:
        reporter.fail("doxygen build", "%d error(s)" % len(errors))
        if verbose:
            for ln in errors[:20]:
                print("    " + ln)
        return
    if warnings:
        # Warnings are reported but do not fail.
        reporter.info("doxygen emitted %d warning(s)" % len(warnings))
        if verbose:
            for ln in warnings[:20]:
                print("    " + ln)
        reporter.ok("doxygen build (warnings only)")
    else:
        reporter.ok("doxygen build (clean)")

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args(argv)

    if args.list:
        decls = collect_declarations()
        for name in sorted(decls):
            print(name)
        return 0

    r = Reporter("doxygen_check", verbose=args.verbose)
    structural_pass(r, args.verbose)
    print()
    doxygen_pass(r, args.verbose)
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
