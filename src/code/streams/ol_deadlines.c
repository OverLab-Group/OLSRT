#include "ol_deadlines.h"
#include <time.h>
#include <unistd.h>

/* Monotonic clock in nanoseconds. Critical for timeouts and scheduling. */
int64_t ol_monotonic_now_ns(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#else
    /* Fallback: CLOCK_REALTIME (not ideal for deadlines due to clock changes) */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
}

/* Deadline creation helpers */
ol_deadline_t ol_deadline_from_ns(int64_t ns_from_now) {
    ol_deadline_t d;
    int64_t now = ol_monotonic_now_ns();
    d.when_ns = now + (ns_from_now > 0 ? ns_from_now : 0);
    return d;
}

ol_deadline_t ol_deadline_from_ms(int64_t ms_from_now) {
    return ol_deadline_from_ns(ms_from_now * 1000000LL);
}

ol_deadline_t ol_deadline_from_sec(double seconds) {
    int64_t ns = (int64_t)(seconds * 1000000000.0);
    return ol_deadline_from_ns(ns);
}

/* Query remaining time to deadline */
bool ol_deadline_expired(ol_deadline_t dl) {
    return ol_monotonic_now_ns() >= dl.when_ns;
}

int64_t ol_deadline_remaining_ns(ol_deadline_t dl) {
    int64_t now = ol_monotonic_now_ns();
    int64_t rem = dl.when_ns - now;
    return rem > 0 ? rem : 0;
}

int64_t ol_deadline_remaining_ms(ol_deadline_t dl) {
    int64_t ns = ol_deadline_remaining_ns(dl);
    return ns / 1000000LL;
}

/* Sleep until deadline using nanosleep for precision. */
void ol_sleep_until(ol_deadline_t dl) {
    int64_t rem_ns = ol_deadline_remaining_ns(dl);
    if (rem_ns <= 0) return;

    struct timespec req;
    req.tv_sec  = rem_ns / 1000000000LL;
    req.tv_nsec = rem_ns % 1000000000LL;

    /* Best-effort sleep; ignore interruptions for simplicity. */
    nanosleep(&req, NULL);
}

/* Clamp poll timeout to int range and reasonable bounds. */
int ol_clamp_poll_timeout_ms(int64_t remaining_ms) {
    if (remaining_ms <= 0) return 0;           // no wait
    if (remaining_ms > 0x7FFFFFFF) return 30000; // cap at 30s to avoid int overflow
    return (int)remaining_ms;
}
