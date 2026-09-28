#!/usr/bin/env python3
"""
OLSRT Wave 1 - build fix v5.

Final strategy:

  1. Remove Qwen-generated files from the tree (git rm if tracked, plain rm
     if untracked). These are:
        - src/code/streams/ol_actor_isolation.c
        - src/code/streams/ol_actor_isolation.h
        - src/code/streams/ol_actor_serialization.c
        - src/code/streams/ol_actor_serialization.h
     The canonical implementations are ol_actor_arena.*, ol_actor_process.*,
     and ol_actor_serialize.*.

  2. Patch ol_green_threads.c (NOT .h) to make libnuma optional via
     __has_include(<numa.h>). If libnuma-dev is not installed, NUMA is
     disabled automatically; if it is installed later, NUMA comes back
     without any code change.

  3. Regenerate build_wave1_config.py with a clean exclusion list.

  4. Reuse the existing verification flow.

Idempotent, safe to re-run.
"""

import argparse
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Qwen files to remove
# ---------------------------------------------------------------------------

QWEN_FILES = [
    "src/code/streams/ol_actor_isolation.c",
    "src/code/streams/ol_actor_isolation.h",
    "src/code/streams/ol_actor_serialization.c",
    "src/code/streams/ol_actor_serialization.h",
]

def is_tracked(root, relpath):
    r = subprocess.run(
        ["git", "ls-files", "--error-unmatch", relpath],
        cwd=str(root), capture_output=True, text=True,
    )
    return r.returncode == 0

def remove_qwen_files(root, dry_run):
    print("Step 1: remove Qwen-generated files")
    print("-" * 60)
    removed = 0
    skipped = 0
    for rel in QWEN_FILES:
        path = root / rel
        if not path.exists():
            print("[SKIP] {} (already absent)".format(rel))
            skipped += 1
            continue
        tracked = is_tracked(root, rel)
        if dry_run:
            print("[DRY]  would {} {}".format(
                "git rm" if tracked else "rm", rel))
            continue
        if tracked:
            r = subprocess.run(
                ["git", "rm", "-f", rel],
                cwd=str(root), capture_output=True, text=True,
            )
            if r.returncode == 0:
                print("[OK]   git rm  {}".format(rel))
                removed += 1
            else:
                print("[FAIL] git rm  {}  -> {}".format(rel, r.stderr.strip()))
        else:
            path.unlink()
            print("[OK]   rm      {}".format(rel))
            removed += 1

    print()
    print("  removed: {}, skipped: {}".format(removed, skipped))
    return 0 if removed + skipped == len(QWEN_FILES) else 1

# ---------------------------------------------------------------------------
# NUMA patch - target is ol_green_threads.c
# ---------------------------------------------------------------------------

NUMA_SEARCH = """    #if defined(__linux__)
        #include <sys/timerfd.h>
        #include <sys/eventfd.h>
        #include <linux/futex.h>
        #include <numa.h>
        #include <numaif.h>
        #define OL_NUMA_AVAILABLE 1"""

NUMA_REPLACE = """    #if defined(__linux__)
        #include <sys/timerfd.h>
        #include <sys/eventfd.h>
        #include <linux/futex.h>
        /* NUMA support is optional. If libnuma-dev is installed, we use it;
         * otherwise the runtime falls back to node-agnostic allocation.
         * The behaviour can also be forced via -DOL_DISABLE_NUMA=1. */
        #if !defined(OL_DISABLE_NUMA) && defined(__has_include) && __has_include(<numa.h>)
            #include <numa.h>
            #include <numaif.h>
            #define OL_NUMA_AVAILABLE 1
        #else
            #define OL_NUMA_AVAILABLE 0
        #endif"""

def patch_numa(root, dry_run):
    print()
    print("Step 2: make libnuma optional in ol_green_threads.c")
    print("-" * 60)
    target = root / "src" / "code" / "streams" / "ol_green_threads.c"
    if not target.exists():
        print("[FAIL] ol_green_threads.c not found")
        return 1

    text = target.read_text(encoding="utf-8")

    if "__has_include(<numa.h>)" in text:
        print("[SKIP] ol_green_threads.c: NUMA guard already present")
        return 0

    if NUMA_SEARCH not in text:
        print("[FAIL] NUMA anchor not found in ol_green_threads.c")
        print("       Manual review needed. Look for `#include <numa.h>`")
        # Show surrounding lines to help debug
        lines = text.split("\n")
        for i, line in enumerate(lines):
            if "numa.h" in line:
                start = max(0, i - 4)
                end = min(len(lines), i + 5)
                print("       context lines {}-{}:".format(start + 1, end))
                for j in range(start, end):
                    print("         {:5d}| {}".format(j + 1, lines[j]))
                break
        return 1

    if dry_run:
        print("[DRY]  would patch NUMA block in ol_green_threads.c")
        return 0

    new = text.replace(NUMA_SEARCH, NUMA_REPLACE, 1)
    backup = target.with_suffix(target.suffix + ".orig")
    if not backup.exists():
        backup.write_text(text, encoding="utf-8")
    target.write_text(new, encoding="utf-8")
    print("[OK]   NUMA guard installed -> backup: {}".format(backup.name))
    return 0

# ---------------------------------------------------------------------------
# Regenerate build_wave1_config.py
# ---------------------------------------------------------------------------

CONFIG_CONTENT = """# Auto-generated by fix_wave1_v5.py
# Consumed by verify.py to configure the sanitizer builds.

EXCLUDE = [
]

# -DOL_DISABLE_NUMA is a belt-and-suspenders flag; the header already
# checks __has_include(<numa.h>) but this makes the intent explicit.
EXTRA_FLAGS = [
    "-DOL_DISABLE_NUMA=1",
]
"""

def write_config(root, dry_run):
    print()
    print("Step 3: reset build_wave1_config.py (no exclusions)")
    print("-" * 60)
    target = root / "build_wave1_config.py"
    if dry_run:
        print("[DRY]  would write " + str(target.name))
        return 0
    target.write_text(CONFIG_CONTENT, encoding="utf-8")
    print("[OK]   wrote " + str(target.name) + " (empty exclusion list)")
    return 0

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
    print("OLSRT root: " + str(root))
    print()

    fails = 0
    fails += remove_qwen_files(root, args.dry_run)
    fails += patch_numa(root, args.dry_run)
    fails += write_config(root, args.dry_run)

    print()
    print("=" * 60)
    print("Failures: {}".format(fails))
    print()
    print("Next: run `python3 verify.py`")
    return 0 if fails == 0 else 2

if __name__ == "__main__":
    sys.exit(main())
