/**
 * @file ol_deadlines.c
 * @brief Deadline utilities for monotonic time, deadline creation, expiration checks, and sleep helpers.
 *
 * This module provides a minimal abstraction for working with timeouts and deadlines
 * in a monotonic time domain. It is designed to support deadline-aware APIs such as
 * condition variable waits, event loops, and scheduling systems.
 *
 * Features:
 * ----------
 * - Monotonic time retrieval in nanoseconds.
 * - Deadline creation from relative durations (ns, ms, sec).
 * - Deadline expiration checks.
 * - Remaining time queries (ns, ms).
 * - Sleep until deadline using nanosleep.
 * - Poll timeout clamping to safe integer ranges.
 *
 * Thread-safety:
 * --------------
 * All functions are thread-safe and reentrant. They do not maintain any internal state.
 *
 * Platform support:
 * -----------------
 * - Uses CLOCK_MONOTONIC if available (POSIX-compliant systems).
 * - Falls back to CLOCK_REALTIME if CLOCK_MONOTONIC is unavailable.
 *
 * Usage example:
 * --------------
 * @code
 *   ol_deadline_t dl = ol_deadline_from_ms(500); // 500ms from now
 *   while (!ol_deadline_expired(dl)) {
 *       // do something
 *   }
 * @endcode
 */

#include "ol_deadlines.h"
#include <time.h>
#include <unistd.h>

/**
 * @brief Get current monotonic time in nanoseconds.
 *
 * Uses CLOCK_MONOTONIC if available, otherwise falls back to CLOCK_REALTIME.
 * This function is used as the base for all deadline calculations.
 *
 * @return Current time in nanoseconds since an unspecified epoch.
 */
int64_t ol_monotonic_now_ns(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
}

/**
 * @brief Create a deadline that is a number of nanoseconds from now.
 *
 * If ns_from_now <= 0, the deadline is set to the current time (i.e., already expired).
 *
 * @param ns_from_now Relative duration in nanoseconds.
 * @return ol_deadline_t representing the absolute deadline.
 */
ol_deadline_t ol_deadline_from_ns(int64_t ns_from_now) {
    ol_deadline_t d;
    int64_t now = ol_monotonic_now_ns();
    d.when_ns = now + (ns_from_now > 0 ? ns_from_now : 0);
    return d;
}

/**
 * @brief Create a deadline that is a number of milliseconds from now.
 *
 * Internally converts milliseconds to nanoseconds and delegates to ol_deadline_from_ns().
 *
 * @param ms_from_now Relative duration in milliseconds.
 * @return ol_deadline_t representing the absolute deadline.
 */
ol_deadline_t ol_deadline_from_ms(int64_t ms_from_now) {
    return ol_deadline_from_ns(ms_from_now * 1000000LL);
}

/**
 * @brief Create a deadline that is a number of seconds from now.
 *
 * Internally converts seconds to nanoseconds and delegates to ol_deadline_from_ns().
 *
 * @param seconds Relative duration in seconds (fractional allowed).
 * @return ol_deadline_t representing the absolute deadline.
 */
ol_deadline_t ol_deadline_from_sec(double seconds) {
    int64_t ns = (int64_t)(seconds * 1000000000.0);
    return ol_deadline_from_ns(ns);
}

/**
 * @brief Check whether a deadline has expired.
 *
 * Compares the current monotonic time to the deadline.
 *
 * @param dl Deadline to check.
 * @return true if current time >= deadline, false otherwise.
 */
bool ol_deadline_expired(ol_deadline_t dl) {
    return ol_monotonic_now_ns() >= dl.when_ns;
}

/**
 * @brief Get remaining time until deadline in nanoseconds.
 *
 * If the deadline has already passed, returns 0.
 *
 * @param dl Deadline to check.
 * @return Remaining time in nanoseconds (>= 0).
 */
int64_t ol_deadline_remaining_ns(ol_deadline_t dl) {
    int64_t now = ol_monotonic_now_ns();
    int64_t rem = dl.when_ns - now;
    return rem > 0 ? rem : 0;
}

/**
 * @brief Get remaining time until deadline in milliseconds.
 *
 * If the deadline has already passed, returns 0.
 *
 * @param dl Deadline to check.
 * @return Remaining time in milliseconds (>= 0).
 */
int64_t ol_deadline_remaining_ms(ol_deadline_t dl) {
    int64_t ns = ol_deadline_remaining_ns(dl);
    return ns / 1000000LL;
}

/**
 * @brief Sleep until the given deadline using nanosleep().
 *
 * If the deadline has already passed, returns immediately.
 * This is a best-effort sleep; interruptions (e.g., signals) are ignored.
 *
 * @param dl Deadline to sleep until.
 */
void ol_sleep_until(ol_deadline_t dl) {
    int64_t rem_ns = ol_deadline_remaining_ns(dl);
    if (rem_ns <= 0) return;

    struct timespec req;
    req.tv_sec  = rem_ns / 1000000000LL;
    req.tv_nsec = rem_ns % 1000000000LL;

    /* Best-effort sleep; ignores EINTR */
    nanosleep(&req, NULL);
}

/**
 * @brief Clamp a poll timeout to a safe integer range.
 *
 * Used to convert a 64-bit timeout (in milliseconds) to a bounded int value
 * suitable for APIs like poll/select that accept int timeouts.
 *
 * Behavior:
 *  - If remaining_ms <= 0: returns 0 (no wait).
 *  - If remaining_ms > INT_MAX: returns 30000 (30 seconds).
 *  - Otherwise: returns (int)remaining_ms.
 *
 * @param remaining_ms Timeout in milliseconds.
 * @return Clamped timeout in milliseconds.
 */
int ol_clamp_poll_timeout_ms(int64_t remaining_ms) {
    if (remaining_ms <= 0) return 0;
    if (remaining_ms > 0x7FFFFFFF) return 30000; // cap at 30s to avoid int overflow
    return (int)remaining_ms;
}