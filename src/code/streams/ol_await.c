#include "ol_await.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

/* ol_await_future is a thin wrapper */
int ol_await_future(ol_future_t *f, int64_t deadline_ns) {
    return ol_future_await(f, deadline_ns);
}

/* Helper: sleep for small interval (ms) while allowing other loop tasks to run.
 * We keep this conservative and portable using nanosleep.
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

/* ol_await_future_with_loop:
 * This function is intended to be called from the event-loop thread when you need to wait on
 * a future but also keep the loop responsive. It polls the future with short time slices,
 * waking the loop between polls so the loop can handle events and continuations.
 *
 * WARNING: This is cooperative. Callers must not call from arbitrary threads.
 */
int ol_await_future_with_loop(ol_event_loop_t *loop, ol_future_t *f, int64_t deadline_ns) {
    if (!f) return -1;

    const long slice_ms = 10; /* 10ms slice */
    int64_t start_ns = ol_monotonic_now_ns();
    int64_t deadline_local = deadline_ns;
    bool use_deadline = (deadline_ns > 0);

    for (;;) {
        int r = ol_future_await(f, (use_deadline ? deadline_local : 0));
        if (r != 0) {
            return r; /* completed or error/timeout */
        }

        /* If we get here, r == 0 => timeout (we passed a finite deadline and it expired).
         * But ol_future_await only returns 0 on timeout; we already returned above.
         * For robustness, poll with very short sleeps instead:
         */
        if (use_deadline) {
            int64_t now_ns = ol_monotonic_now_ns();
            if (now_ns >= deadline_local) return 0; /* timeout */
        }

        /* Give control to the event loop by sleeping a small amount to allow loop polling/wakeup.
         * If an explicit loop-run-once API exists, prefer that. Here we call ol_event_loop_wake
         * so that other threads/continuations may unblock the loop; then sleep briefly.
         */
        if (loop) (void)ol_event_loop_wake(loop);
        small_sleep_ms(slice_ms);
    }

    /* unreachable */
    return -1;
}
