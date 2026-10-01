#!/usr/bin/env python3
"""
ci/lint_check.py — run every detected C linter.

Detected backends: clang-tidy, cppcheck, sparse, smatch, splint.

By default, `error`-severity findings fail the check; `warning`,
`style`, `performance`, and `portability` findings are reported but do
not fail. Under --strict, any finding fails. This mirrors the
warnings_check budget approach: CI stays green while the codebase
converges on the strict state during v1.3.3.
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from _common import Reporter, chdir_to_root, has_tool, run

INCLUDES = ["includes", "includes/code", "includes/code/streams",
            "includes/runtime"]

SEVERITY_RE = re.compile(
    r"\b(error|warning|style|performance|portability|note)\s*:",
    re.IGNORECASE,
)

def sources():
    return sorted(str(p) for p in
                  Path("src/code/streams").glob("*.c"))

def compile_commands_exists():
    return (Path("compile_commands.json").exists() or
            Path("build/compile_commands.json").exists())

def classify_findings(lines):
    """Return (errors, others) lists from a stream of tool output."""
    errors = []
    others = []
    for ln in lines:
        m = SEVERITY_RE.search(ln)
        if not m:
            continue
        sev = m.group(1).lower()
        if sev == "error":
            errors.append(ln)
        elif sev in ("warning", "style", "performance",
                     "portability"):
            others.append(ln)
        # 'note' lines are attached to a preceding finding; skip.
    return errors, others

def report_findings(reporter, name, errors, others, verbose, strict,
                    max_show=10):
    """Shared reporting logic for a single linter."""
    if errors:
        reporter.fail("%s: %d error(s)" % (name, len(errors)))
        for ln in errors[:max_show]:
            print("      " + ln)
        if len(errors) > max_show:
            print("      ... and %d more" % (len(errors) - max_show))
        return
    if others:
        reporter.info("%s: %d non-error finding(s)" % (name, len(others)))
        for ln in others[:max_show]:
            print("      " + ln)
        if len(others) > max_show:
            print("      ... and %d more" % (len(others) - max_show))
        if strict:
            reporter.fail("%s: %d finding(s) under --strict"
                          % (name, len(others)))
        else:
            reporter.ok("%s (findings reported)" % name)
        return
    reporter.ok(name)

def run_clang_tidy(reporter, verbose, strict):
    if not has_tool("clang-tidy"):
        return False
    if not compile_commands_exists():
        reporter.skip("clang-tidy", "no compile_commands.json")
        return True
    errors, others = [], []
    for src in sources():
        rc, out, err = run(["clang-tidy", src, "--", "-std=gnu11"],
                           timeout=120)
        e, o = classify_findings((out + err).splitlines())
        errors.extend(e)
        others.extend(o)
    report_findings(reporter, "clang-tidy", errors, others,
                    verbose, strict)
    return True

def run_cppcheck(reporter, verbose, strict):
    if not has_tool("cppcheck"):
        return False
    cmd = ["cppcheck",
           "--enable=warning,style,performance,portability",
           "--quiet",
           "--suppress=missingIncludeSystem",
           "--inline-suppr"]
    cmd += ["-I" + i for i in INCLUDES]
    cmd += sources()
    rc, out, err = run(cmd, timeout=300)
    errors, others = classify_findings((out + err).splitlines())
    report_findings(reporter, "cppcheck", errors, others,
                    verbose, strict)
    return True

def run_sparse(reporter, verbose, strict):
    if not has_tool("sparse"):
        return False
    cmd = ["sparse", "-Wall"]
    cmd += ["-I" + i for i in INCLUDES]
    cmd += sources()
    rc, out, err = run(cmd, timeout=180)
    errors, others = classify_findings((out + err).splitlines())
    report_findings(reporter, "sparse", errors, others,
                    verbose, strict)
    return True

def run_smatch(reporter, verbose, strict):
    if not has_tool("smatch"):
        return False
    cmd = ["smatch"]
    cmd += ["-I" + i for i in INCLUDES]
    cmd += sources()
    rc, out, err = run(cmd, timeout=300)
    errors, others = classify_findings((out + err).splitlines())
    report_findings(reporter, "smatch", errors, others,
                    verbose, strict)
    return True

def run_splint(reporter, verbose, strict):
    if not has_tool("splint"):
        return False
    cmd = ["splint", "+quiet"]
    cmd += ["-I" + i for i in INCLUDES]
    cmd += sources()
    rc, out, err = run(cmd, timeout=180)
    errors, others = classify_findings((out + err).splitlines())
    report_findings(reporter, "splint", errors, others,
                    verbose, strict)
    return True

def main(argv=None):
    chdir_to_root()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--strict", action="store_true",
                    help="Fail on warning-level findings too.")
    args = ap.parse_args(argv)

    r = Reporter("lint_check", verbose=args.verbose)
    ran = False
    for fn in (run_clang_tidy, run_cppcheck, run_sparse,
               run_smatch, run_splint):
        if fn(r, args.verbose, args.strict):
            ran = True
    if not ran:
        r.skip("all linters", "none installed")
    return r.exit_code()

if __name__ == "__main__":
    sys.exit(main())
