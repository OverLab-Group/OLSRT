/**
 * @file ol_await.c
 * @brief Utilities for awaiting futures, including cooperative waiting inside an event loop.
 *
 * Overview
 * --------
 * - ol_await_future: thin wrapper around ol_future_await for convenience.
 * - ol_await_future_with_loop: cooperative waiting on a future while keeping an event loop responsive.
 *
 * Rationale
 * ---------
 * Some callers run on the event-loop thread and must not block the loop indefinitely.
 * ol_await_future_with_loop polls the future in short slices and yields control to the
 * event loop between polls so other loop tasks can progress.
 *
 * Contracts and warnings
 * ----------------------
 * - ol_await_future_with_loop must only be called from the event-loop thread (cooperative model).
 * - This function is not suitable for arbitrary threads.
 * - The polling slice is conservative (10 ms) to balance responsiveness and CPU usage.
 *
 * Testing and tooling notes
 * -------------------------
 * - Unit tests should validate correct return codes for success, timeout, and error.
 * - Use sanitizers to detect misuse (e.g., calling from non-loop threads may cause deadlocks).
 */

#include "ol_await.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

/**
 * @brief Thin wrapper that awaits a future until deadline.
 *
 * Delegates to ol_future_await which returns:
 * - 1 on success (fulfilled/rejected/canceled)
 * - -3 on timeout
 * - -1 on error
 *
 * @param f Future handle
 * @param deadline_ns Absolute deadline in nanoseconds (0 = infinite)
 * @return int See ol_future_await semantics
 */
int ol_await_future(ol_future_t *f, int64_t deadline_ns) {
    return ol_future_await(f, deadline_ns);
}

/**
 * @brief Sleep for a small interval (milliseconds) in a portable way.
 *
 * This helper uses nanosleep on POSIX and Sleep on Windows. It is intended for short
 * cooperative yields inside the event loop polling loop.
 *
 * @param ms Milliseconds to sleep (non-negative)
 */
static void small_sleep_ms(long ms) {
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/**
 * @brief Await a future from inside the event-loop thread while keeping the loop responsive.
 *
 * Behavior:
 * - Polls the future using ol_future_await with the provided deadline.
 * - If the future is not ready, wakes the event loop (ol_event_loop_wake) and sleeps briefly
 *   to allow the loop to process other events.
 * - This is cooperative: callers must ensure they are running on the loop thread.
 *
 * Return values mirror ol_future_await:
 * - 1 on completion (fulfilled/rejected/canceled)
 * - -3 on timeout
 * - -1 on error
 *
 * @param loop Event loop instance (may be NULL if caller only wants polling behavior)
 * @param f Future to await (must be non-NULL)
 * @param deadline_ns Absolute deadline in nanoseconds (0 = infinite)
 * @return int See ol_future_await semantics
 */
int ol_await_future_with_loop(ol_event_loop_t *loop, ol_future_t *f, int64_t deadline_ns) {
    if (!f) return -1;

    const long slice_ms = 10; /* 10 ms polling slice */
    int64_t start_ns = ol_monotonic_now_ns();
    int64_t deadline_local = deadline_ns;
    bool use_deadline = (deadline_ns > 0);

    for (;;) {
        int r = ol_future_await(f, (use_deadline ? deadline_local : 0));
        if (r != 0) {
            /* Completed, timed out, or error */
            return r;
        }

        /* Defensive deadline check: if deadline passed, return timeout */
        if (use_deadline) {
            int64_t now_ns = ol_monotonic_now_ns();
            if (now_ns >= deadline_local) return -3; /* timeout */
        }

        /* Give control to the event loop: wake it and sleep briefly to allow processing. */
        if (loop) (void)ol_event_loop_wake(loop);
        small_sleep_ms(slice_ms);
    }

    /* unreachable */
    return -1;
}