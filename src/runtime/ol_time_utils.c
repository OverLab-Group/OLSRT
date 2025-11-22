#include "ol_time_utils.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* -----------------------------------------------------
 * Get current time in nanoseconds since epoch
 * Uses clock_gettime for high-resolution timestamps.
 * ----------------------------------------------------- */
int64_t ol_time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * OL_NSEC_PER_SEC + ts.tv_nsec;
}

/* -----------------------------------------------------
 * Sleep for the given duration
 * Converts nanoseconds to seconds/nanoseconds for nanosleep.
 * ----------------------------------------------------- */
void ol_time_sleep(ol_duration_t duration) {
    if (duration.nanoseconds <= 0) return;

    struct timespec req;
    req.tv_sec  = duration.nanoseconds / OL_NSEC_PER_SEC;
    req.tv_nsec = duration.nanoseconds % OL_NSEC_PER_SEC;

    nanosleep(&req, NULL);
}

/* -----------------------------------------------------
 * Convert seconds to ol_duration_t
 * ----------------------------------------------------- */
ol_duration_t ol_time_from_seconds(double seconds) {
    ol_duration_t d;
    d.nanoseconds = (int64_t)(seconds * OL_NSEC_PER_SEC);
    return d;
}

/* -----------------------------------------------------
 * Convert milliseconds to ol_duration_t
 * ----------------------------------------------------- */
ol_duration_t ol_time_from_milliseconds(int64_t msec) {
    ol_duration_t d;
    d.nanoseconds = msec * (OL_NSEC_PER_SEC / OL_MSEC_PER_SEC);
    return d;
}

/* -----------------------------------------------------
 * Convert ol_duration_t to milliseconds
 * ----------------------------------------------------- */
int64_t ol_time_to_milliseconds(ol_duration_t duration) {
    return duration.nanoseconds / (OL_NSEC_PER_SEC / OL_MSEC_PER_SEC);
}

/* -----------------------------------------------------
 * Format current time as human-readable string
 * Example output: "2025-11-21 21:48:00"
 * ----------------------------------------------------- */
void ol_time_format_now(char *buffer, size_t size) {
    if (buffer == NULL || size == 0) return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}
