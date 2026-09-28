#!/usr/bin/env python3
"""
OLSRT Wave 1 - Surgical patch applier (markdown-safe version).

Finds the OLSRT root by scanning up from cwd for `includes/olsrt.h`,
then applies surgical patches to the C source files.

This file is written to avoid any pattern of the form `__word__`
inside string literals, because chat markdown renderers turn those
into `<strong>word</strong>` and corrupt the file.

The C keyword for GCC atomic builtins is spelled here as `DUNDER + "atomic_..."`
via a helper, so the source-of-truth on disk has the real `__atomic_...`
without ever appearing as `__atomic_...` in the rendering layer.

Usage:
    python3 apply_patches.py [--dry-run] [--root PATH]
"""

import argparse
import sys
from pathlib import Path

# The double-underscore token, spelled in a way that cannot be corrupted
# by markdown emphasis parsing.
DUNDER = "_" + "_"

def _d(s):
    """Replace the placeholder '@@' in s with a literal double underscore."""
    return s.replace("@@", DUNDER)

# ============================================================================
# Patch definitions. All literal `__` in the search/replace text is written
# as `@@` and expanded via _d() at load time.
# ============================================================================

PATCHES = [
    # ------------------------------------------------------------------------
    # 01 - Promise leak in ol_actor_ask
    # ------------------------------------------------------------------------
    {
        "id": "01",
        "name": "promise_leak_in_ol_actor_ask",
        "file": "src/code/streams/ol_actor.c",
        "changes": [
            {
                "search": """    envelope->payload = msg;
    envelope->reply = promise;
    envelope->sender = ol_actor_self();
    envelope->ask_id = ol_monotonic_now_ns();
    
    /* Store in pending asks for timeout handling (future enhancement) */
    ol_mutex_lock(&actor->ask_mutex);
    ol_hashmap_put(actor->pending_asks, &envelope->ask_id, 
                  sizeof(uint64_t), envelope);
    ol_mutex_unlock(&actor->ask_mutex);
    
    /* Send envelope to actor */
    int send_result = ol_actor_send(actor, envelope);
    if (send_result != 0) {
        /* Failed to send - clean up */
        ol_mutex_lock(&actor->ask_mutex);
        ol_hashmap_remove(actor->pending_asks, &envelope->ask_id, 
                         sizeof(uint64_t));
        ol_mutex_unlock(&actor->ask_mutex);
        
        ol_actor_reply_cancel(envelope);
        ol_future_destroy(future);
        return NULL;
    }
    
    return future;
}""",
                "replace": """    envelope->payload = msg;
    envelope->reply = promise;
    envelope->sender = ol_actor_self();
    envelope->ask_id = ol_monotonic_now_ns();
    
    /* NOTE(v1.3.1): The previous implementation inserted the envelope into
     * actor->pending_asks for "future timeout handling", but no code path
     * ever removed entries from that hashmap. Every ol_actor_ask() therefore
     * leaked one envelope + one promise. Since nothing functional read from
     * the map, it has been removed.
     *
     * When ask-timeout support is reintroduced, ownership must be transferred
     * through the reply promise with an ol_future_then() continuation that
     * cancels the promise and frees the envelope exactly once.
     */
    
    /* Send envelope to actor */
    int send_result = ol_actor_send(actor, envelope);
    if (send_result != 0) {
        /* Failed to send - clean up */
        ol_actor_reply_cancel(envelope);
        ol_future_destroy(future);
        return NULL;
    }
    
    return future;
}""",
            },
        ],
    },

    # ------------------------------------------------------------------------
    # 02 - ol_actor_send_timeout: replace busy-wait with cond-timed-wait
    # ------------------------------------------------------------------------
    {
        "id": "02",
        "name": "actor_send_timeout_cond_wait",
        "file": "src/code/streams/ol_actor.c",
        "changes": [
            {
                "search": """int ol_actor_send_timeout(ol_actor_t* actor, void* msg, uint32_t timeout_ms) {
    if (actor == NULL) {
        return -1;
    }
    
    ol_deadline_t deadline = ol_deadline_from_ms(timeout_ms);
    
    while (!ol_deadline_expired(deadline)) {
        /* Try to send (non-blocking) */
        int result = ol_actor_try_send(actor, msg);
        if (result != 0) {
            return result; /* Success or permanent error */
        }
        
        /* Wait a short time and retry */
#if defined(_WIN32)
        Sleep(1);
#else
        usleep(1000);
#endif
    }
    
    /* Timeout expired - clean up message */
    if (actor->msg_dtor) {
        actor->msg_dtor(msg);
    }
    return 0; /* Would block (timeout) */
}""",
                "replace": """int ol_actor_send_timeout(ol_actor_t* actor, void* msg, uint32_t timeout_ms) {
    if (actor == NULL || msg == NULL) {
        if (actor && actor->msg_dtor && msg) {
            actor->msg_dtor(msg);
        }
        return -1;
    }
    
    /* Fast-path state check */
    if (actor->state & (ACTOR_STATE_CLOSED | ACTOR_STATE_CRASHED)) {
        if (actor->msg_dtor) actor->msg_dtor(msg);
        return -1;
    }
    
    /* timeout_ms == 0 means infinite wait (matches ol_actor_send semantics) */
    const bool infinite = (timeout_ms == 0);
    ol_deadline_t deadline;
    if (infinite) {
        deadline.when_ns = 0;
    } else {
        deadline = ol_deadline_from_ms(timeout_ms);
    }
    
    for (;;) {
        /* Try lock-free ring buffer first */
        if (actor_mailbox_try_send_fast(actor->mailbox, msg)) {
            return 0;
        }
        
        ol_mutex_lock(&actor->mailbox->mutex);
        
        /* Re-check state under lock (actor may have been closed) */
        if (actor->state & (ACTOR_STATE_CLOSED | ACTOR_STATE_CRASHED)) {
            ol_mutex_unlock(&actor->mailbox->mutex);
            if (actor->msg_dtor) actor->msg_dtor(msg);
            return -1;
        }
        
        /* Slow path: overflow list has space? */
        if (actor->mailbox->overflow_count < actor->mailbox->capacity) {
            actor->mailbox->overflow_list[actor->mailbox->overflow_count++] = msg;
            actor->mailbox->overflow_events++;
            actor->mailbox->total_messages++;
            ol_cond_signal(&actor->mailbox->not_empty);
            ol_mutex_unlock(&actor->mailbox->mutex);
            return 0;
        }
        
        /* Mailbox full - wait for space with proper deadline */
        if (!infinite && ol_deadline_expired(deadline)) {
            ol_mutex_unlock(&actor->mailbox->mutex);
            if (actor->msg_dtor) actor->msg_dtor(msg);
            return -3; /* OL_TIMEOUT */
        }
        
        int r = ol_cond_wait_until(&actor->mailbox->not_full,
                                   &actor->mailbox->mutex,
                                   infinite ? 0 : deadline.when_ns);
        ol_mutex_unlock(&actor->mailbox->mutex);
        
        if (r == 0) {
            /* Timed out while waiting */
            if (actor->msg_dtor) actor->msg_dtor(msg);
            return -3;
        }
        /* r == 1 (signaled) or r < 0 (spurious): loop and retry */
    }
}""",
            },
        ],
    },

    # ------------------------------------------------------------------------
    # 03a - Memory ordering in lock-free mailbox
    # ------------------------------------------------------------------------
    {
        "id": "03a",
        "name": "mailbox_send_fast_atomics",
        "file": "src/code/streams/ol_actor.c",
        "changes": [
            {
                "search": _d("""static bool actor_mailbox_try_send_fast(actor_mailbox_t* mb, void* msg) {
    size_t current_tail = mb->tail;
    size_t next_tail = (current_tail + 1) % mb->capacity;
    
    /* Check if buffer has space (lock-free read of head) */
    if (next_tail == mb->head) {
        return false;
    }
    
    /* Store message in ring buffer */
    mb->ring_buffer[current_tail] = msg;
    
    /* Atomic update of tail (release semantics for visibility) */
    @@atomic_store_n(&mb->tail, next_tail, @@ATOMIC_RELEASE);
    
    /* Update statistics */
    mb->total_messages++;
    size_t size = (next_tail > mb->head) ? 
                 (next_tail - mb->head) : 
                 (mb->capacity - mb->head + next_tail);
    if (size > mb->peak_size) mb->peak_size = size;
    
    return true;
}"""),
                "replace": _d("""static bool actor_mailbox_try_send_fast(actor_mailbox_t* mb, void* msg) {
    /* All shared fields must be read with proper memory ordering to avoid
     * torn reads and stale values on weakly-ordered CPUs (ARM64, POWER,
     * RISC-V). The producer is single-writer for 'tail', so RELAXED is
     * sufficient for the local read; the consumer's 'head' must be ACQUIRE
     * to observe a consistent view before we decide the buffer is full. */
    size_t current_tail = @@atomic_load_n(&mb->tail, @@ATOMIC_RELAXED);
    size_t head         = @@atomic_load_n(&mb->head, @@ATOMIC_ACQUIRE);
    size_t next_tail    = (current_tail + 1) % mb->capacity;
    
    /* Buffer full? */
    if (next_tail == head) {
        return false;
    }
    
    /* Store message first, then publish tail with RELEASE so the consumer
     * observes the message before the updated tail. */
    mb->ring_buffer[current_tail] = msg;
    @@atomic_store_n(&mb->tail, next_tail, @@ATOMIC_RELEASE);
    
    /* Non-atomic stats - producer is single-writer for these */
    mb->total_messages++;
    size_t size = (next_tail > head)
                  ? (next_tail - head)
                  : (mb->capacity - head + next_tail);
    if (size > mb->peak_size) mb->peak_size = size;
    
    return true;
}"""),
            },
            {
                "search": _d("""        /* Try fast path first (lock-free ring buffer) */
        size_t current_head = mb->head;
        if (current_head != mb->tail) {
            /* Messages available in ring buffer */
            buffer[count++] = mb->ring_buffer[current_head];
            mb->ring_buffer[current_head] = NULL;
            
            /* Update head atomically (release semantics) */
            size_t next_head = (current_head + 1) % mb->capacity;
            @@atomic_store_n(&mb->head, next_head, @@ATOMIC_RELEASE);
            
            /* Signal not_full if needed (buffer now has space) */
            if ((next_head + 1) % mb->capacity == mb->tail) {
                ol_cond_signal(&mb->not_full);
            }
            
            continue;
        }"""),
                "replace": _d("""        /* Try fast path first (lock-free ring buffer).
         * Consumer is single-writer for 'head'. Producer's 'tail' must be
         * read with ACQUIRE to ensure the message payload written by the
         * producer is visible before we copy it out. */
        size_t current_head = @@atomic_load_n(&mb->head, @@ATOMIC_RELAXED);
        size_t current_tail = @@atomic_load_n(&mb->tail, @@ATOMIC_ACQUIRE);
        if (current_head != current_tail) {
            /* Messages available in ring buffer */
            buffer[count++] = mb->ring_buffer[current_head];
            mb->ring_buffer[current_head] = NULL;
            
            size_t next_head = (current_head + 1) % mb->capacity;
            @@atomic_store_n(&mb->head, next_head, @@ATOMIC_RELEASE);
            
            /* Signal not_full if we just freed a slot */
            if ((next_head + 1) % mb->capacity == current_tail) {
                ol_cond_signal(&mb->not_full);
            }
            
            continue;
        }"""),
            },
        ],
    },

    # ------------------------------------------------------------------------
    # 04 - Arena ownership validation
    # ------------------------------------------------------------------------
    {
        "id": "04",
        "name": "arena_free_ownership_check",
        "file": "src/code/streams/ol_actor_arena.c",
        "changes": [
            {
                "search": """void ol_arena_free(ol_arena_t* arena, void* ptr) {
    if (!arena || !ptr) {
        return;
    }
    
    ol_mutex_lock(&arena->mutex);
    
    /* Get allocation header (before user pointer) */
    alloc_header_t* header = (alloc_header_t*)((char*)ptr - sizeof(alloc_header_t));""",
                "replace": """void ol_arena_free(ol_arena_t* arena, void* ptr) {
    if (!arena || !ptr) {
        return;
    }
    
    /* Ownership validation: reject pointers that do not belong to this arena.
     * Freeing a pointer from a different arena would read garbage from the
     * supposed header, potentially corrupting the free list and causing
     * undefined behavior. This is a common bug in multi-actor / multi-arena
     * setups where a message allocated in arena A is mistakenly freed into
     * arena B.
     *
     * If the pointer is out of range we return silently. In debug builds
     * this would be an assertion failure; in release builds we prefer to
     * leak rather than corrupt. */
    {
        uintptr_t addr      = (uintptr_t)ptr;
        uintptr_t pool_base = (uintptr_t)arena->memory_pool;
        uintptr_t pool_end  = pool_base + arena->pool_size;
        if (addr < pool_base || addr >= pool_end) {
            return; /* Not our pointer */
        }
    }
    
    ol_mutex_lock(&arena->mutex);
    
    /* Get allocation header (before user pointer) */
    alloc_header_t* header = (alloc_header_t*)((char*)ptr - sizeof(alloc_header_t));""",
            },
        ],
    },

    # ------------------------------------------------------------------------
    # 07 - Supervisor shutdown hard bound
    # ------------------------------------------------------------------------
    {
        "id": "07",
        "name": "supervisor_shutdown_hard_bound",
        "file": "src/code/streams/ol_supervisor.c",
        "changes": [
            {
                "search": """    /* Wait for shutdown */
    ol_deadline_t deadline = ol_deadline_from_ms(
        graceful ? supervisor->config.shutdown_timeout_ms : 1000);
    
    while (supervisor->state != SUPERVISOR_STATE_STOPPED) {
        if (ol_deadline_expired(deadline)) {
            break;
        }
#if defined(_WIN32)
        Sleep(10);
#else
        usleep(10000);
#endif
    }
    
    /* Destroy supervisor process */
    if (supervisor->process) {
        ol_process_destroy(supervisor->process, OL_EXIT_NORMAL);
        supervisor->process = NULL;
    }
    
    supervisor->state = SUPERVISOR_STATE_STOPPED;
    
    return 0;
}""",
                "replace": """    /* Wait for shutdown, but never longer than the configured timeout plus
     * a small grace period. A stuck child must not block supervisor teardown
     * indefinitely. After the deadline we forcibly destroy the supervisor
     * process and mark the supervisor as stopped. */
    int64_t timeout_ms = graceful ? supervisor->config.shutdown_timeout_ms : 1000;
    if (timeout_ms <= 0) timeout_ms = 1000; /* never wait forever */
    ol_deadline_t deadline = ol_deadline_from_ms(timeout_ms + 500 /* grace */);
    
    while (supervisor->state != SUPERVISOR_STATE_STOPPED) {
        if (ol_deadline_expired(deadline)) {
            /* Hard timeout - escalate by killing the process directly. */
            break;
        }
#if defined(_WIN32)
        Sleep(10);
#else
        usleep(10000);
#endif
    }
    
    /* Destroy supervisor process (idempotent on already-dead process) */
    if (supervisor->process) {
        ol_process_destroy(supervisor->process, OL_EXIT_KILL);
        supervisor->process = NULL;
    }
    
    supervisor->state = SUPERVISOR_STATE_STOPPED;
    
    return 0;
}""",
            },
        ],
    },
]

# ============================================================================
# Matching / application machinery
# ============================================================================

def find_normalized(content, search):
    content_lines = content.split("\n")
    search_lines = [l.strip() for l in search.split("\n")]
    while search_lines and not search_lines[0]:
        search_lines.pop(0)
    while search_lines and not search_lines[-1]:
        search_lines.pop()
    if not search_lines:
        return None

    n = len(search_lines)
    for i in range(len(content_lines) - n + 1):
        ok = True
        for j in range(n):
            if content_lines[i + j].strip() != search_lines[j]:
                ok = False
                break
        if ok:
            return i, i + n
    return None

def apply_change(content, search, replace):
    loc = find_normalized(content, search)
    if loc is None:
        if find_normalized(content, replace) is not None:
            return content, "already_applied"
        return None, "not_found"

    start, end = loc
    content_lines = content.split("\n")
    replace_lines = replace.split("\n")
    new_lines = content_lines[:start] + replace_lines + content_lines[end:]
    return "\n".join(new_lines), "applied"

# ============================================================================
# Entry point
# ============================================================================

def find_olsrt_root(explicit):
    if explicit is not None:
        if (explicit / "includes" / "olsrt.h").exists():
            return explicit
        return None
    for base in [Path.cwd()] + list(Path.cwd().parents):
        if (base / "includes" / "olsrt.h").exists() and (base / "src").exists():
            return base
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=None,
                    help="Path to OLSRT root (auto-detected if omitted)")
    ap.add_argument("--dry-run", action="store_true",
                    help="Report changes without writing files")
    args = ap.parse_args()

    root = find_olsrt_root(args.root)
    if root is None:
        print("ERROR: OLSRT root not found. Run from within an OLSRT checkout or pass --root.")
        return 1

    print("OLSRT root: {}".format(root))
    if args.dry_run:
        print("(dry-run mode: no files will be written)")

    by_file = {}
    for p in PATCHES:
        by_file.setdefault(p["file"], []).append(p)

    total_applied = 0
    total_skipped = 0
    total_failed = 0

    for relpath, patches in by_file.items():
        target = root / relpath
        if not target.exists():
            print("[FAIL] {}: file not found".format(relpath))
            total_failed += len(patches)
            continue

        original = target.read_text(encoding="utf-8")
        content = original

        for patch in patches:
            for idx, change in enumerate(patch["changes"]):
                result, status = apply_change(content, change["search"], change["replace"])
                if status == "applied":
                    content = result
                    total_applied += 1
                    print("[OK]   {} {} (change #{})".format(patch["id"], patch["name"], idx))
                elif status == "already_applied":
                    total_skipped += 1
                    print("[SKIP] {} {} (change #{}) - already applied".format(
                        patch["id"], patch["name"], idx))
                else:
                    total_failed += 1
                    print("[FAIL] {} {} (change #{}) - search string not found".format(
                        patch["id"], patch["name"], idx))

        if content != original and not args.dry_run:
            backup = target.with_suffix(target.suffix + ".orig")
            if not backup.exists():
                backup.write_text(original, encoding="utf-8")
                print("       backup -> {}".format(backup.relative_to(root)))
            target.write_text(content, encoding="utf-8")
            print("       wrote  -> {}".format(relpath))

    print()
    print("Applied:  {}".format(total_applied))
    print("Skipped:  {}".format(total_skipped))
    print("Failed:   {}".format(total_failed))

    return 0 if total_failed == 0 else 2

if __name__ == "__main__":
    sys.exit(main())
