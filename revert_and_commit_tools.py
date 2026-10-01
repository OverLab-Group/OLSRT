#!/usr/bin/env python3
"""
revert_and_commit_tools.py

Three tasks:

1. Revert demos/07_http_server.c. The local change (MAX_REQ 3 -> 100)
   was a personal test and should not be part of the repository.

2. Track verify.py and diagnose_doxygen.py. Both are useful project
   tools. verify.py may be listed in .gitignore as a "personal script"
   from an earlier policy; if so, force-add it and note this in the
   commit message.

3. Report the final state so the user can confirm before pushing.

No push. No changes to any other file.

Run from the repository root:
    python3 revert_and_commit_tools.py
"""

import subprocess
import sys

def run(*args, check=False):
    r = subprocess.run(list(args), capture_output=True, text=True,
                       check=check)
    return r.returncode, r.stdout, r.stderr

def git(*args, check=False):
    return run("git", *args, check=check)

def section(title):
    print()
    print("== %s ==" % title)

# ---------------------------------------------------------------------
# 1. Revert the local change in demos/07_http_server.c
# ---------------------------------------------------------------------

def revert_http_server():
    section("1. revert demos/07_http_server.c")

    target = "demos/07_http_server.c"

    rc, status, _ = git("status", "--porcelain", "--", target)
    if not status.strip():
        print("  no change to revert")
        return

    print("  current status: %r" % status.strip())

    rc, diff, _ = git("diff", "--", target)
    if diff:
        print("  diff:")
        for line in diff.splitlines():
            print("    " + line)

    rc, _, err = git("checkout", "HEAD", "--", target)
    if rc != 0:
        print("  revert failed: %s" % err.strip())
        return
    print("  reverted to HEAD")

# ---------------------------------------------------------------------
# 2. Track the two tools
# ---------------------------------------------------------------------

def is_ignored(path):
    rc, out, _ = git("check-ignore", path)
    return rc == 0  # check-ignore exits 0 if the path is ignored

def track_tools():
    section("2. track tool files")

    tools = [
        ("verify.py",
         "test runner: builds the library under ASan/UBSan and TSan\n"
         "and executes the Wave 1 regression suite in each."),
        ("diagnose_doxygen.py",
         "diagnostic: reports the Doxygen state (RICH / BRIEF /\n"
         "MISSING) of every public function in the actor, process,\n"
         "and green-thread modules."),
    ]

    for path, purpose in tools:
        print("  %s" % path)
        if is_ignored(path):
            print("    currently ignored by .gitignore; force-adding")
            rc, _, err = git("add", "-f", "--", path)
            if rc != 0:
                print("    force-add failed: %s" % err.strip())
                continue
        else:
            rc, _, err = git("add", "--", path)
            if rc != 0:
                print("    add failed: %s" % err.strip())
                continue
        print("    staged")

    # Commit only if something is actually staged for these paths.
    rc, staged, _ = git("diff", "--cached", "--name-only")
    relevant = [ln for ln in staged.splitlines()
                if ln in {p for p, _ in tools}]
    if not relevant:
        print("  nothing to commit (already tracked?)")
        return

    message = (
        "tools: track verify.py and diagnose_doxygen.py\n"
        "\n"
        "Both scripts became part of the release workflow during\n"
        "the v1.3.2 stabilization effort:\n"
        "\n"
        "  verify.py            run the regression suite under\n"
        "                       ASan, UBSan, and TSan\n"
        "  diagnose_doxygen.py  report the Doxygen state of every\n"
        "                       public function\n"
        "\n"
        "verify.py is listed in .gitignore as a personal script from\n"
        "an earlier policy; it is force-added here because the project\n"
        "now depends on it for release verification. The .gitignore\n"
        "entry should be removed in a later cleanup.\n"
    )
    rc, _, err = git("commit", "-m", message)
    if rc != 0:
        print("  commit failed: %s" % err.strip())
        return
    print("  committed (not pushed)")

# ---------------------------------------------------------------------
# 3. Report
# ---------------------------------------------------------------------

def report():
    section("3. final state")

    rc, status, _ = git("status", "--short")
    if status.strip():
        print("  git status --short:")
        for line in status.splitlines():
            print("    " + line)
    else:
        print("  working tree is clean")

    print()
    rc, log, _ = git("log", "--oneline", "-12")
    print("  recent commits:")
    for line in log.splitlines():
        print("    " + line)

    print()
    print("  All changes are local. Inspect with:")
    print("    git log --oneline -12")
    print("    git diff HEAD~6..HEAD --stat")
    print("  Push when satisfied:")
    print("    git push origin main")

def main():
    print()
    revert_http_server()
    track_tools()
    report()
    print()
    print("====================================================")
    print("Done. Working tree should now be clean.")
    print("Next release milestone: v1.3.3 (Dataflow Cleanup).")
    print("====================================================")
    return 0

if __name__ == "__main__":
    sys.exit(main())
