#!/usr/bin/env python3
"""
OLSRT Wave 1 build fix - resolves two distinct issues:

  A) Platform detection collision: `OL_PLATFORM_WINDOWS` is defined to 0
     on Linux by some headers, so `#if defined(OL_PLATFORM_WINDOWS)` takes
     the Windows branch on Linux. Fix: replace every `#if defined(...)` style
     check with compiler built-in macros `_WIN32` / `_WIN64`.

  B) Nested function in ol_supervisor.c: `child_process_entry` is defined
     inside `supervisor_create_child_process`. Nested functions are a GCC
     extension not supported by clang. Fix: hoist it to file scope.

Idempotent. Safe to re-run. Creates `.orig` backups.
"""

import argparse
import re
import sys
from pathlib import Path

DUNDER = "_" + "_"

# ---------------------------------------------------------------------------
# Fix A: platform-detection checks
# ---------------------------------------------------------------------------

PAT_POS = re.compile(r'#if\s+defined\s*\(\s*OL_PLATFORM_WINDOWS\s*\)')
PAT_NEG = re.compile(r'#if\s+!\s*defined\s*\(\s*OL_PLATFORM_WINDOWS\s*\)')
PAT_IFDEF = re.compile(r'#ifdef\s+OL_PLATFORM_WINDOWS\b')
PAT_IFNDEF = re.compile(r'#ifndef\s+OL_PLATFORM_WINDOWS\b')

REPL_POS = '#if defined(_WIN32) || defined(_WIN64)'
REPL_NEG = '#if !(defined(_WIN32) || defined(_WIN64))'

SRC_EXTS = {".c", ".h"}

def rewrite_platform_checks(text):
    """Return (new_text, n_changes)."""
    n = 0
    text, k = PAT_POS.subn(REPL_POS, text); n += k
    text, k = PAT_NEG.subn(REPL_NEG, text); n += k
    text, k = PAT_IFDEF.subn(REPL_POS, text); n += k
    text, k = PAT_IFNDEF.subn(REPL_NEG, text); n += k
    return text, n

def walk_source_tree(root):
    for base in ("src", "includes"):
        b = root / base
        if not b.is_dir():
            continue
        for p in b.rglob("*"):
            if p.is_file() and p.suffix in SRC_EXTS:
                yield p

# ---------------------------------------------------------------------------
# Fix B: nested function hoisting in ol_supervisor.c
# ---------------------------------------------------------------------------

NESTED_FN_SEARCH = """    /* Process entry function wrapper */
    static void child_process_entry(ol_process_t* process, void* arg) {
        child_info_t* child = (child_info_t*)arg;
        if (!child || !child->spec.fn) return;
        
        /* Update child state */
        child->state = CHILD_STATE_RUNNING;
        child->start_time = ol_monotonic_now_ns();
        
        /* Execute child function */
        int result = child->spec.fn(child->spec.arg);
        
        /* Update exit status */
        child->exit_status = result;
        
        /* Update state */
        if (result == 0) {
            child->state = CHILD_STATE_STOPPED;
        } else {
            child->state = CHILD_STATE_CRASHED;
            child->last_crash_time = ol_monotonic_now_ns();
            child->crash_count++;
        }
        
        /* Update uptime statistics */
        if (child->start_time > 0) {
            uint64_t uptime_ms = (ol_monotonic_now_ns() - child->start_time) / 1000000;
            child->total_uptime_ms += uptime_ms;
        }
    }
    
    /* Create child process with isolation */
    ol_process_t* process = ol_process_create(
        child_process_entry,"""

NESTED_FN_REPLACE = """    /* Create child process with isolation.
     * NOTE: the process entry function has been hoisted to file scope
     * (supervisor_child_process_entry) because nested functions are a
     * GCC extension not supported by clang. */
    ol_process_t* process = ol_process_create(
        supervisor_child_process_entry,"""

ANCHOR = """static ol_process_t* supervisor_create_child_process(
    ol_supervisor_t* supervisor,
    const ol_child_spec_t* spec,
    child_info_t* child_info) {"""

HOISTED_FN = """/* File-scope child process entry.
 * Hoisted from a nested function inside supervisor_create_child_process
 * for clang compatibility. */
static void supervisor_child_process_entry(ol_process_t* process, void* arg) {
    (void)process;
    child_info_t* child = (child_info_t*)arg;
    if (!child || !child->spec.fn) return;
    
    /* Update child state */
    child->state = CHILD_STATE_RUNNING;
    child->start_time = ol_monotonic_now_ns();
    
    /* Execute child function */
    int result = child->spec.fn(child->spec.arg);
    
    /* Update exit status */
    child->exit_status = result;
    
    /* Update state */
    if (result == 0) {
        child->state = CHILD_STATE_STOPPED;
    } else {
        child->state = CHILD_STATE_CRASHED;
        child->last_crash_time = ol_monotonic_now_ns();
        child->crash_count++;
    }
    
    /* Update uptime statistics */
    if (child->start_time > 0) {
        uint64_t uptime_ms = (ol_monotonic_now_ns() - child->start_time) / 1000000;
        child->total_uptime_ms += uptime_ms;
    }
}

static ol_process_t* supervisor_create_child_process(
    ol_supervisor_t* supervisor,
    const ol_child_spec_t* spec,
    child_info_t* child_info) {"""

def fix_supervisor_nested(root):
    target = root / "src" / "code" / "streams" / "ol_supervisor.c"
    if not target.exists():
        print("[SKIP] ol_supervisor.c not found")
        return 0, 0

    text = target.read_text(encoding="utf-8")

    if "supervisor_child_process_entry" in text:
        print("[SKIP] ol_supervisor.c: nested function already hoisted")
        return 0, 0

    if NESTED_FN_SEARCH not in text:
        print("[FAIL] ol_supervisor.c: nested-function anchor not found")
        return 1, 0

    # 1) Replace the nested-function block + call-site
    text = text.replace(NESTED_FN_SEARCH, NESTED_FN_REPLACE, 1)

    # 2) Insert the hoisted function right before supervisor_create_child_process
    if ANCHOR not in text:
        print("[FAIL] ol_supervisor.c: function signature anchor not found")
        return 1, 0
    text = text.replace(ANCHOR, HOISTED_FN, 1)

    backup = target.with_suffix(target.suffix + ".orig")
    if not backup.exists():
        # If a .orig already exists from an earlier patch, keep it
        backup = target.with_suffix(target.suffix + ".orig2")
    backup.write_text(target.read_text(encoding="utf-8"), encoding="utf-8")
    target.write_text(text, encoding="utf-8")
    print("[OK]   ol_supervisor.c: hoisted nested function -> {}".format(
        backup.name))
    return 0, 1

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def find_root(start):
    base = start.resolve()
    for c in [base] + list(base.parents)[:6]:
        if (c / "includes" / "olsrt.h").exists() and (c / "src").is_dir():
            return c
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=None)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = args.root or find_root(Path.cwd())
    if root is None:
        print("ERROR: OLSRT root not found")
        return 1
    print("OLSRT root: {}".format(root))
    if args.dry_run:
        print("(dry-run mode)")

    total_files = 0
    total_changes = 0
    total_failures = 0

    # Fix A: walk the tree and rewrite platform checks
    print()
    print("Fix A: platform-detection checks")
    print("-" * 60)
    for path in walk_source_tree(root):
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        new_text, n = rewrite_platform_checks(text)
        if n == 0:
            continue
        total_files += 1
        total_changes += n
        rel = path.relative_to(root)
        if args.dry_run:
            print("[DRY] {}  ({} replacement{})".format(
                rel, n, "s" if n != 1 else ""))
        else:
            backup = path.with_suffix(path.suffix + ".orig")
            if not backup.exists():
                backup.write_text(text, encoding="utf-8")
            path.write_text(new_text, encoding="utf-8")
            print("[OK]  {}  ({} replacement{})".format(
                rel, n, "s" if n != 1 else ""))

    print()
    print("Fix B: nested function in ol_supervisor.c")
    print("-" * 60)
    f, c = fix_supervisor_nested(root)
    total_failures += f
    total_changes += c

    print()
    print("=" * 60)
    print("Files touched:      {}".format(total_files))
    print("Total changes:      {}".format(total_changes))
    print("Failures:           {}".format(total_failures))

    return 0 if total_failures == 0 else 2

if __name__ == "__main__":
    sys.exit(main())
