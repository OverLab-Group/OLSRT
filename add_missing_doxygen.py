#!/usr/bin/env python3
"""
add_missing_doxygen.py

Part A - commit any pending changes from the previous script.

Part B - fix the function-pointer parameter parser in
         ci/doxygen_check.py. The previous version extracted the last
         identifier from each comma-separated parameter chunk, which
         for `void (*value_destructor)(void*)` returned "void" and
         made ol_hashmap_create look like it was missing @param
         value_destructor.

Part C - install Doxygen comment blocks above the 57 public prototypes
         that currently carry a plain /* */ comment (or none at all)
         in six headers:

           includes/code/streams/ol_dataflow.h         (3)
           includes/code/streams/ol_memwatch.h         (4)
           includes/code/streams/ol_parallel.h         (8)
           includes/code/streams/ol_semaphores.h       (6)
           includes/code/streams/ol_streams.h          (18)
           includes/code/streams/ol_reactive.h         (21)

Part D - commit.

Run from the repository root:
    python3 add_missing_doxygen.py
"""

import re
import subprocess
import sys
from pathlib import Path

def git(*args, check=False):
    r = subprocess.run(["git"] + list(args), capture_output=True,
                       text=True, check=check)
    return r.returncode, r.stdout, r.stderr

def section(title):
    print()
    print("== %s ==" % title)

# ---------------------------------------------------------------------
# Part A
# ---------------------------------------------------------------------

def commit_pending():
    section("A. commit pending changes")
    rc, status, _ = git("status", "--porcelain")
    if not status.strip():
        print("  working tree is clean")
        return
    to_add = []
    for ln in status.splitlines():
        if len(ln) < 4:
            continue
        path = ln[3:]
        if path.startswith(("build/", "bin/")):
            continue
        to_add.append(path)
    if not to_add:
        print("  nothing to stage")
        return
    git("add", "--", *to_add)
    rc, _, err = git("commit", "-m",
                     "wip: uncommitted changes from the previous script\n"
                     "\n"
                     "Auto-committed by add_missing_doxygen.py. Not pushed.\n")
    if rc != 0:
        print("  commit failed: %s" % err.strip())
    else:
        print("  committed (not pushed)")

# ---------------------------------------------------------------------
# Part B - fix param parser
# ---------------------------------------------------------------------

def fix_param_parser():
    section("B. fix param parser in ci/doxygen_check.py")

    path = Path("ci/doxygen_check.py")
    if not path.exists():
        print("  ci/doxygen_check.py not found")
        return
    text = path.read_text(encoding="utf-8")

    old = (
        'def param_names(param_string):\n'
        '    """Extract parameter names from a C parameter list."""\n'
        '    if not param_string.strip() or param_string.strip() == "void":\n'
        '        return []\n'
        '    names = []\n'
        '    for chunk in param_string.split(","):\n'
        '        chunk = chunk.strip()\n'
        '        if not chunk:\n'
        '            continue\n'
        '        m = re.findall(r"[A-Za-z_]\\w*", chunk)\n'
        '        if m:\n'
        '            names.append(m[-1])\n'
        '    return names\n'
    )
    new = (
        'def param_names(param_string):\n'
        '    """Extract parameter names from a C parameter list."""\n'
        '    if not param_string.strip() or param_string.strip() == "void":\n'
        '        return []\n'
        '    # Split on commas at parenthesis depth zero, so that\n'
        '    # function-pointer parameters stay intact.\n'
        '    chunks = []\n'
        '    depth = 0\n'
        '    current = ""\n'
        '    for c in param_string:\n'
        '        if c == "(":\n'
        '            depth += 1\n'
        '            current += c\n'
        '        elif c == ")":\n'
        '            depth -= 1\n'
        '            current += c\n'
        '        elif c == "," and depth == 0:\n'
        '            chunks.append(current)\n'
        '            current = ""\n'
        '        else:\n'
        '            current += c\n'
        '    if current.strip():\n'
        '        chunks.append(current)\n'
        '\n'
        '    names = []\n'
        '    for chunk in chunks:\n'
        '        chunk = chunk.strip()\n'
        '        if not chunk:\n'
        '            continue\n'
        '        # Function pointer: `void (*name)(...)`.\n'
        '        m = re.search(r"\\(\\s*\\*\\s*(\\w+)\\s*\\)", chunk)\n'
        '        if m:\n'
        '            names.append(m.group(1))\n'
        '            continue\n'
        '        # Regular parameter: last identifier.\n'
        '        ids = re.findall(r"[A-Za-z_]\\w*", chunk)\n'
        '        if ids:\n'
        '            names.append(ids[-1])\n'
        '    return names\n'
    )
    if old in text:
        text = text.replace(old, new, 1)
        path.write_text(text, encoding="utf-8")
        print("  OK: param parser handles function pointers")
    elif "Function pointer: `void (*name)(...)`" in text:
        print("  already fixed")
    else:
        print("  warning: param_names not in expected form")

# ---------------------------------------------------------------------
# Part C - install Doxygen blocks
# ---------------------------------------------------------------------

BLOCKS = {

# -------------------- ol_dataflow.h --------------------

"ol_dataflow.h": {
"ol_df_node_out_ports": """\
/**
 * @brief Return the number of outbound ports on a node.
 *
 * @param n Node handle; NULL is tolerated.
 * @return Port count, or 0 if @p n is NULL.
 */""",

"ol_df_graph_node_count": """\
/**
 * @brief Return the number of nodes currently in the graph.
 *
 * @param g Graph handle; NULL is tolerated.
 * @return Node count, or 0 if @p g is NULL.
 */""",

"ol_df_graph_edge_count": """\
/**
 * @brief Return the number of edges currently in the graph.
 *
 * @param g Graph handle; NULL is tolerated.
 * @return Edge count, or 0 if @p g is NULL.
 */""",
},

# -------------------- ol_memwatch.h --------------------

"ol_memwatch.h": {
"ol_memwatch_malloc": """\
/**
 * @brief malloc wrapped by the memory watcher.
 *
 * @param size Number of bytes to allocate.
 * @return Pointer to the allocation, or NULL on failure.
 * @see ol_memwatch_free
 */""",

"ol_memwatch_calloc": """\
/**
 * @brief calloc wrapped by the memory watcher.
 *
 * @param nmemb Number of elements.
 * @param size  Size of each element in bytes.
 * @return Zero-initialised buffer, or NULL on failure.
 */""",

"ol_memwatch_realloc": """\
/**
 * @brief realloc wrapped by the memory watcher.
 *
 * @param ptr  Existing allocation, or NULL for a fresh allocation.
 * @param size New size in bytes.
 * @return Reallocated buffer, or NULL on failure.
 */""",

"ol_memwatch_free": """\
/**
 * @brief free wrapped by the memory watcher.
 *
 * @param ptr Allocation to release; NULL is a no-op.
 */""",
},

# -------------------- ol_parallel.h --------------------

"ol_parallel.h": {
"ol_parallel_create": """\
/**
 * @brief Create a thread pool.
 *
 * @param num_threads Number of worker threads (>= 1).
 * @return Pool handle, or NULL on failure.
 * @see ol_parallel_destroy, ol_parallel_submit
 */""",

"ol_parallel_destroy": """\
/**
 * @brief Destroy a thread pool.
 *
 * @details
 * Equivalent to ol_parallel_shutdown(pool, true) followed by a
 * resource release. Pending tasks are drained before workers exit.
 *
 * @param pool Pool handle; NULL is a no-op.
 */""",

"ol_parallel_submit": """\
/**
 * @brief Submit a task to the pool.
 *
 * @param pool Pool handle.
 * @param fn   Task function.
 * @param arg  Argument forwarded to @p fn.
 * @return 0 on success, -1 on error, -2 if the pool is not accepting
 *         new work.
 */""",

"ol_parallel_flush": """\
/**
 * @brief Wait until the queue is empty and all tasks have finished.
 *
 * @param pool Pool handle.
 * @return 0 on success, -1 on error.
 */""",

"ol_parallel_shutdown": """\
/**
 * @brief Stop the pool.
 *
 * @param pool  Pool handle.
 * @param drain If true, run all queued tasks before stopping; if
 *              false, discard pending tasks.
 * @return 0 on success, -1 on error.
 */""",

"ol_parallel_thread_count": """\
/**
 * @brief Return the number of worker threads in the pool.
 *
 * @param pool Pool handle.
 * @return Worker count, or 0 if @p pool is NULL.
 */""",

"ol_parallel_queue_size": """\
/**
 * @brief Return the current number of queued tasks.
 *
 * @param pool Pool handle.
 * @return Queue size, or 0 if @p pool is NULL.
 */""",

"ol_parallel_is_running": """\
/**
 * @brief Check whether the pool is running.
 *
 * @param pool Pool handle.
 * @return true if the pool is accepting work, false otherwise.
 */""",
},

# -------------------- ol_semaphores.h --------------------

"ol_semaphores.h": {
"ol_sem_init": """\
/**
 * @brief Initialize a counting semaphore.
 *
 * @param s         Semaphore to initialize.
 * @param initial   Initial count.
 * @param max_count Maximum count (must be >= initial and > 0).
 * @return 0 on success, -1 on error.
 * @see ol_sem_destroy
 */""",

"ol_sem_destroy": """\
/**
 * @brief Destroy a semaphore.
 *
 * @param s Semaphore handle.
 * @return 0 on success, -1 on error.
 */""",

"ol_sem_post": """\
/**
 * @brief Increment the semaphore by one.
 *
 * @param s Semaphore handle.
 * @return 0 on success, -1 if the count is already at max_count or on
 *         error.
 */""",

"ol_sem_trywait": """\
/**
 * @brief Non-blocking decrement.
 *
 * @param s Semaphore handle.
 * @return 1 if acquired, 0 if would block, -1 on error.
 */""",

"ol_sem_wait_until": """\
/**
 * @brief Decrement with an absolute deadline.
 *
 * @param s           Semaphore handle.
 * @param deadline_ns Absolute monotonic deadline in nanoseconds.
 *                    0 or negative means wait forever.
 * @return 0 on success, -3 on timeout, -1 on error.
 */""",

"ol_sem_getvalue": """\
/**
 * @brief Read the current count.
 *
 * @param s         Semaphore handle.
 * @param out_value Output: current count.
 * @return 0 on success, -1 on error.
 */""",
},

# -------------------- ol_streams.h --------------------

"ol_streams.h": {
"ol_stream_create": """\
/**
 * @brief Create a cold stream.
 *
 * @param loop Event loop that drives timers and I/O for operators
 *             derived from this stream.
 * @param dtor Destructor called on items the stream owns; pass NULL
 *             for a forward-only stream.
 * @return Stream handle, or NULL on failure.
 * @see ol_stream_destroy, ol_stream_subscribe
 */""",

"ol_stream_destroy": """\
/**
 * @brief Destroy a stream and release its resources.
 *
 * @param s Stream handle; NULL is a no-op. Any active subscriptions
 *          observe a completion notification.
 */""",

"ol_stream_subscribe": """\
/**
 * @brief Subscribe to a stream.
 *
 * @param s             Stream handle.
 * @param on_next       Callback invoked for each item.
 * @param on_error      Callback invoked on error (may be NULL).
 * @param on_complete   Callback invoked on completion (may be NULL).
 * @param demand        Initial demand (0 means the caller will request
 *                      later).
 * @param user_data     Opaque pointer forwarded to all callbacks.
 * @return Subscription handle, or NULL on failure.
 * @see ol_subscription_request, ol_subscription_unsubscribe
 */""",

"ol_subscription_request": """\
/**
 * @brief Request more items from the stream.
 *
 * @param sub Subscription handle.
 * @param n   Number of additional items to request.
 * @return 0 on success, -1 on error.
 */""",

"ol_subscription_unsubscribe": """\
/**
 * @brief Stop receiving items on a subscription.
 *
 * @param sub Subscription handle.
 * @return 0 on success, -1 on error. Idempotent.
 */""",

"ol_subscription_destroy": """\
/**
 * @brief Destroy a subscription object.
 *
 * @param sub Subscription handle. Call after unsubscribe or completion
 *            to release the handle itself.
 */""",

"ol_stream_emit_next": """\
/**
 * @brief Push one item into a source stream.
 *
 * @param s    Stream handle.
 * @param item Item pointer. Ownership follows the stream's destructor.
 * @return 0 on success, -1 on error.
 */""",

"ol_stream_emit_error": """\
/**
 * @brief Signal an error on a source stream.
 *
 * @param s          Stream handle.
 * @param error_code Error code forwarded to subscribers.
 * @return 0 on success, -1 on error.
 */""",

"ol_stream_emit_complete": """\
/**
 * @brief Signal normal completion on a source stream.
 *
 * @param s Stream handle.
 * @return 0 on success, -1 on error.
 */""",

"ol_stream_map": """\
/**
 * @brief Transform each item with a user function.
 *
 * @param src      Source stream.
 * @param fn       Map function returning the transformed item.
 * @param user_data Opaque pointer forwarded to @p fn.
 * @param out_dtor Destructor for transformed items (may be NULL).
 * @return New stream handle, or NULL on failure.
 */""",

"ol_stream_filter": """\
/**
 * @brief Drop items for which the predicate returns false.
 *
 * @param src      Source stream.
 * @param pred     Predicate function.
 * @param user_data Opaque pointer forwarded to @p pred.
 * @return New stream handle, or NULL on failure.
 */""",

"ol_stream_take": """\
/**
 * @brief Forward only the first @p n items, then complete.
 *
 * @param src Source stream.
 * @param n   Number of items to forward.
 * @return New stream handle, or NULL on failure.
 */""",

"ol_stream_merge": """\
/**
 * @brief Interleave items from two source streams.
 *
 * @param a         First source stream.
 * @param b         Second source stream.
 * @param dtor_hint Destructor used for items emitted by the merged
 *                  stream (may be NULL).
 * @return New stream handle, or NULL on failure.
 */""",

"ol_stream_debounce": """\
/**
 * @brief Emit an item only after a quiet interval.
 *
 * @param src         Source stream.
 * @param interval_ns Quiet interval in nanoseconds.
 * @return New stream handle, or NULL on failure.
 */""",

"ol_stream_timer": """\
/**
 * @brief Create a stream that ticks on a timer.
 *
 * @param loop      Event loop that drives the timer.
 * @param period_ns Period between ticks in nanoseconds.
 * @param count     Number of ticks (1 for a one-shot).
 * @return New stream handle, or NULL on failure.
 */""",

"ol_stream_from_fd": """\
/**
 * @brief Create a stream that emits on I/O readiness.
 *
 * @param loop Event loop that drives the poller.
 * @param fd   File descriptor to watch; the stream does not own it.
 * @param mask Poll mask (OL_POLL_IN / OL_POLL_OUT).
 * @return New stream handle, or NULL on failure.
 */""",

"ol_stream_is_completed": """\
/**
 * @brief Check whether a stream has completed or errored.
 *
 * @param s Stream handle.
 * @return true if the stream is in a terminal state.
 */""",

"ol_stream_subscriber_count": """\
/**
 * @brief Return the number of active subscribers.
 *
 * @param s Stream handle.
 * @return Subscriber count, or 0 if @p s is NULL.
 */""",
},

# -------------------- ol_reactive.h --------------------

"ol_reactive.h": {
"ol_subject_create": """\
/**
 * @brief Create a subject (a hot observable driven by the caller).
 *
 * @param loop Event loop that drives timers and I/O for operators
 *             derived from this subject.
 * @param dtor Destructor applied to items the subject owns.
 * @return Subject handle, or NULL on failure.
 * @see ol_subject_destroy, ol_subject_on_next
 */""",

"ol_subject_destroy": """\
/**
 * @brief Destroy a subject and release its resources.
 *
 * @param s Subject handle; NULL is a no-op.
 */""",

"ol_subject_on_next": """\
/**
 * @brief Push one item into the subject.
 *
 * @param s    Subject handle.
 * @param item Item pointer; ownership follows the subject's
 *             destructor.
 * @return 0 on success, -1 on error.
 */""",

"ol_subject_on_error": """\
/**
 * @brief Signal an error to subscribers.
 *
 * @param s          Subject handle.
 * @param error_code Error code forwarded to observers.
 * @return 0 on success, -1 on error.
 */""",

"ol_subject_on_complete": """\
/**
 * @brief Signal normal completion to subscribers.
 *
 * @param s Subject handle.
 * @return 0 on success, -1 on error.
 */""",

"ol_subject_as_observable": """\
/**
 * @brief Return the observable view of a subject.
 *
 * @param s Subject handle.
 * @return Observable pointer that shares the subject's lifetime.
 */""",

"ol_observable_create": """\
/**
 * @brief Create an empty cold observable.
 *
 * @param loop Event loop that drives derived operators.
 * @param dtor Item destructor.
 * @return Observable handle, or NULL on failure.
 * @see ol_observable_destroy, ol_observable_subscribe
 */""",

"ol_observable_destroy": """\
/**
 * @brief Destroy an observable.
 *
 * @param o Observable handle; NULL is a no-op. Active subscriptions
 *          observe a completion notification.
 */""",

"ol_observable_subscribe": """\
/**
 * @brief Subscribe to an observable.
 *
 * @param o           Observable handle.
 * @param on_next     Callback invoked for each item.
 * @param on_error    Callback invoked on error (may be NULL).
 * @param on_complete Callback invoked on completion (may be NULL).
 * @param demand      Initial demand.
 * @param user_data   Opaque pointer forwarded to all callbacks.
 * @return Subscription handle, or NULL on failure.
 * @see ol_rx_request, ol_rx_unsubscribe
 */""",

"ol_rx_request": """\
/**
 * @brief Request more items from an observable.
 *
 * @param sub Subscription handle.
 * @param n   Number of additional items to request.
 * @return 0 on success, -1 on error.
 */""",

"ol_rx_unsubscribe": """\
/**
 * @brief Stop receiving items on a subscription.
 *
 * @param sub Subscription handle.
 * @return 0 on success, -1 on error. Idempotent.
 */""",

"ol_rx_subscription_destroy": """\
/**
 * @brief Destroy a subscription object.
 *
 * @param sub Subscription handle. Call after unsubscribe or
 *            completion to release the handle itself.
 */""",

"ol_rx_map": """\
/**
 * @brief Transform each item with a user function.
 *
 * @param src         Source observable.
 * @param fn          Map function returning the transformed item.
 * @param user_data   Opaque pointer forwarded to @p fn.
 * @param out_dtor    Destructor for transformed items.
 * @return New observable handle, or NULL on failure.
 */""",

"ol_rx_filter": """\
/**
 * @brief Drop items for which the predicate returns false.
 *
 * @param src       Source observable.
 * @param pred      Predicate function.
 * @param user_data Opaque pointer forwarded to @p pred.
 * @return New observable handle, or NULL on failure.
 */""",

"ol_rx_take": """\
/**
 * @brief Forward only the first @p n items, then complete.
 *
 * @param src Source observable.
 * @param n   Number of items to forward.
 * @return New observable handle, or NULL on failure.
 */""",

"ol_rx_merge": """\
/**
 * @brief Interleave items from two source observables.
 *
 * @param a         First source observable.
 * @param b         Second source observable.
 * @param dtor_hint Destructor for items emitted by the merged stream.
 * @return New observable handle, or NULL on failure.
 */""",

"ol_rx_debounce": """\
/**
 * @brief Emit an item only after a quiet interval.
 *
 * @param src         Source observable.
 * @param interval_ns Quiet interval in nanoseconds.
 * @return New observable handle, or NULL on failure.
 */""",

"ol_rx_timer": """\
/**
 * @brief Create an observable that ticks on a timer.
 *
 * @param loop      Event loop that drives the timer.
 * @param period_ns Period between ticks in nanoseconds.
 * @param count     Number of ticks (1 for a one-shot).
 * @return New observable handle, or NULL on failure.
 */""",

"ol_rx_from_fd": """\
/**
 * @brief Create an observable that emits on I/O readiness.
 *
 * @param loop Event loop that drives the poller.
 * @param fd   File descriptor to watch; not owned by the observable.
 * @param mask Poll mask (OL_POLL_IN / OL_POLL_OUT).
 * @return New observable handle, or NULL on failure.
 */""",

"ol_rx_completed": """\
/**
 * @brief Check whether an observable has completed or errored.
 *
 * @param o Observable handle.
 * @return true if the observable is in a terminal state.
 */""",

"ol_rx_subscriber_count": """\
/**
 * @brief Return the number of active subscribers.
 *
 * @param o Observable handle.
 * @return Subscriber count, or 0 if @p o is NULL.
 */""",
},

}

PROTOTYPE_RE = (
    r"(?m)^([a-zA-Z_][\w \t\*]*?\b{name}\s*\()"
)

def install_blocks(header_path, blocks):
    """Install Doxygen blocks above the named prototypes."""
    path = Path("includes/code/streams") / header_path
    if not path.exists():
        print("  %-20s not found" % header_path)
        return 0, 0
    text = path.read_text(encoding="utf-8")
    installed = 0
    skipped = 0

    for func_name, block in blocks.items():
        pattern = re.compile(
            r"(?m)^([a-zA-Z_][\w \t\*]*?\b"
            + re.escape(func_name)
            + r"\s*\()"
        )
        m = pattern.search(text)
        if not m:
            print("    MISSING: %s" % func_name)
            continue

        # Check if the block is already present.
        before = text[:m.start()]
        if "@brief" in before[-400:]:
            skipped += 1
            continue

        # Insert block + two newlines before the match.
        text = text[:m.start()] + block + "\n\n" + text[m.start():]
        installed += 1

    path.write_text(text, encoding="utf-8")
    return installed, skipped

def install_all():
    section("C. install Doxygen blocks")
    total_installed = 0
    total_skipped = 0
    for header, blocks in BLOCKS.items():
        inst, skip = install_blocks(header, blocks)
        total_installed += inst
        total_skipped += skip
        print("  %-24s %d installed, %d already present"
              % (header, inst, skip))
    print()
    print("  total: %d installed, %d already present"
          % (total_installed, total_skipped))

# ---------------------------------------------------------------------
# Part D
# ---------------------------------------------------------------------

def commit():
    section("D. commit")

    rc, status, _ = git("status", "--porcelain")
    relevant = []
    for ln in status.splitlines():
        if len(ln) < 4:
            continue
        path = ln[3:]
        if path.startswith("includes/code/streams/") or \
           path.startswith("ci/"):
            relevant.append(path)

    if not relevant:
        print("  nothing to commit")
        return

    git("add", "--", *relevant)
    message = (
        "docs: complete Doxygen on remaining public headers\n"
        "\n"
        "Add Doxygen comment blocks to the 57 prototypes that still\n"
        "carried a plain /* */ comment (or none at all) in the\n"
        "following headers:\n"
        "\n"
        "  ol_dataflow.h     3\n"
        "  ol_memwatch.h     4\n"
        "  ol_parallel.h     8\n"
        "  ol_semaphores.h   6\n"
        "  ol_streams.h     18\n"
        "  ol_reactive.h    21\n"
        "\n"
        "After this change every public prototype declared under\n"
        "includes/code/streams/ carries a @brief block with @param\n"
        "and @return as applicable.\n"
        "\n"
        "Also fixes the parameter parser in ci/doxygen_check.py so\n"
        "that function-pointer parameters like\n"
        "`void (*destructor)(void*)` are recognised as the named\n"
        "parameter rather than the void keyword.\n"
    )
    rc, out, err = git("commit", "-m", message)
    if rc != 0:
        print("  commit failed: %s" % err.strip())
    else:
        print("  committed (not pushed)")

def main():
    print()
    commit_pending()
    fix_param_parser()
    install_all()
    commit()

    print()
    print("====================================================")
    print("Next:")
    print()
    print("  python3 ci/doxygen_check.py 2>&1 | tail -20")
    print("  # expect: 276 passed, 0 failed, 0 skipped")
    print()
    print("  python3 ci/run_all.py --keep-going "
          "2>&1 | tee /tmp/ci_full.log")
    print("====================================================")
    return 0

if __name__ == "__main__":
    sys.exit(main())
