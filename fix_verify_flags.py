#!/usr/bin/env python3
"""
Patch verify.py: replace strict -std=c11 with -std=gnu11 and add the
POSIX feature macros that OLSRT needs (GNU_SOURCE covers sem_timedwait,
pthread_rwlock_t, usleep, and the asm/nested-function extensions).

Idempotent: rerunning it is safe.
"""

import sys
from pathlib import Path

OLD_FLAGS = '"-std=c11"'
NEW_FLAGS = '"-std=gnu11", "-D_GNU_SOURCE", "-D_POSIX_C_SOURCE=200809L"'

def main():
    here = Path(__file__).resolve().parent
    target = here / "verify.py"
    if not target.exists():
        print("[FAIL] verify.py not found next to this script")
        return 1

    text = target.read_text(encoding="utf-8")
    if NEW_FLAGS in text:
        print("[SKIP] verify.py already patched")
        return 0

    count = text.count(OLD_FLAGS)
    if count == 0:
        print("[FAIL] could not find " + OLD_FLAGS + " in verify.py")
        return 2

    text = text.replace(OLD_FLAGS, NEW_FLAGS)
    target.write_text(text, encoding="utf-8")
    print("[OK] patched {} occurrence(s) in verify.py".format(count))
    return 0

if __name__ == "__main__":
    sys.exit(main())
