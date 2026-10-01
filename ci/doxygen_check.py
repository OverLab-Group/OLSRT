#!/usr/bin/env python3
"""
ci/doxygen_check.py — strict Doxygen coverage check.

Two passes:

1. Structural. For every function prototype declared in a public header
   under includes/code/streams/, find the Doxygen comment block that
   documents it. In C, the convention is to write the block above the
   prototype in the header; Doxygen merges that with the definition.
   The check therefore looks first at the header, and falls back to the
   definition in the .c file only when the header carries no block.

   Classification:
       MISSING  no @brief anywhere
       PARTIAL  has @brief but is missing @param / @return (as applicable)
       RICH     @brief plus every applicable tag

   The tool fails if any public prototype is MISSING or PARTIAL.

2. Doxygen build. Run `doxygen Doxyfile` and count warnings. Warnings
   are reported but do not fail the check by themselves; the
   structural pass is the source of truth.
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
    # Split on commas at parenthesis depth zero, so that
    # function-pointer parameters stay intact.
    chunks = []
    depth = 0
    current = ""
    for c in param_string:
        if c == "(":
            depth += 1
            current += c
        elif c == ")":
            depth -= 1
            current += c
        elif c == "," and depth == 0:
            chunks.append(current)
            current = ""
        else:
            current += c
    if current.strip():
        chunks.append(current)

    names = []
    for chunk in chunks:
        chunk = chunk.strip()
        if not chunk:
            continue
        # Function pointer: `void (*name)(...)`.
        m = re.search(r"\(\s*\*\s*(\w+)\s*\)", chunk)
        if m:
            names.append(m.group(1))
            continue
        # Regular parameter: last identifier.
        ids = re.findall(r"[A-Za-z_]\w*", chunk)
        if ids:
            names.append(ids[-1])
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
    """Return name -> (header, params, has_return)."""
    decls = {}
    for hdir in HEADER_DIRS:
        for hpath in sorted(Path(hdir).glob("*.h")):
            text = hpath.read_text(encoding="utf-8")
            for m in PROTOTYPE_PATTERN.finditer(text):
                name = m.group("name")
                if name in decls:
                    continue
                params = param_names(m.group("params"))
                line = text[m.start():text.find("\n", m.start())]
                head = line.split(name, 1)[0]
                has_return = "void" not in head
                decls[name] = (str(hpath), params, has_return)
    return decls

def collect_definitions():
    """Return name -> (source_path, text, position)."""
    defs = {}
    for src in sorted(Path("src/code/streams").glob("*.c")):
        text = src.read_text(encoding="utf-8")
        for m in DEFINITION_PATTERN.finditer(text):
            name = m.group("name")
            if name in defs:
                continue
            defs[name] = (str(src), text, m.start())
    return defs

def find_documentation(header_path, header_pos, c_path, c_pos):
    """Look for a Doxygen block first in the header, then in the .c.

    Returns (block_text, where) where `where` is 'header', 'source', or
    None if no block was found.
    """
    # Try header first.
    htext = Path(header_path).read_text(encoding="utf-8")
    loc = find_comment_above(htext, header_pos)
    if loc is not None:
        block = htext[loc[0]:loc[1]]
        if "@brief" in block:
            return block, "header"

    # Fall back to source.
    if c_path and c_pos is not None:
        ctext = Path(c_path).read_text(encoding="utf-8")
        loc = find_comment_above(ctext, c_pos)
        if loc is not None:
            block = ctext[loc[0]:loc[1]]
            if "@brief" in block:
                return block, "source"

    return None, None

def collect_header_positions():
    """Return name -> (header, position_in_header)."""
    positions = {}
    for hdir in HEADER_DIRS:
        for hpath in sorted(Path(hdir).glob("*.h")):
            text = hpath.read_text(encoding="utf-8")
            for m in PROTOTYPE_PATTERN.finditer(text):
                name = m.group("name")
                if name in positions:
                    continue
                positions[name] = (str(hpath), m.start())
    return positions

def structural_pass(reporter, verbose):
    decls = collect_declarations()
    defs = collect_definitions()
    header_pos = collect_header_positions()

    reporter.info("public prototypes: %d" % len(decls))
    reporter.info("definitions found: %d" % len(defs))
    print()

    for name, (hdr, params, has_return) in sorted(decls.items()):
        hpos = header_pos.get(name, (hdr, None))[1]
        c_path, c_pos = None, None
        if name in defs:
            c_path, _, c_pos = defs[name]

        block, where = find_documentation(hdr, hpos, c_path, c_pos)
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
