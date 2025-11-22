#ifndef OL_TIME_UTILS_H
#define OL_TIME_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* -----------------------------------------------------
 * Time constants
 * These constants are important for consistency across
 * runtime modules when dealing with time units.
 * ----------------------------------------------------- */
#define OL_MSEC_PER_SEC   1000ULL
#define OL_USEC_PER_SEC   1000000ULL
#define OL_NSEC_PER_SEC   1000000000ULL

/* -----------------------------------------------------
 * Duration structure
 * Represents a time duration in nanoseconds.
 * This abstraction simplifies handling of time intervals.
 * ----------------------------------------------------- */
typedef struct {
    int64_t nanoseconds; // Duration in nanoseconds
} ol_duration_t;

/* -----------------------------------------------------
 * Function prototypes
 * These functions provide utilities for timestamps,
 * sleeping, and duration conversions.
 * ----------------------------------------------------- */

/**
 * @brief Get current time in nanoseconds since epoch.
 * @return Current time as int64_t nanoseconds.
 */
int64_t ol_time_now_ns(void);

/**
 * @brief Sleep for the given duration.
 * @param duration Duration in nanoseconds.
 */
void ol_time_sleep(ol_duration_t duration);

/**
 * @brief Convert seconds to ol_duration_t.
 * @param seconds Number of seconds.
 * @return Duration struct.
 */
ol_duration_t ol_time_from_seconds(double seconds);

/**
 * @brief Convert milliseconds to ol_duration_t.
 * @param msec Number of milliseconds.
 * @return Duration struct.
 */
ol_duration_t ol_time_from_milliseconds(int64_t msec);

/**
 * @brief Convert ol_duration_t to milliseconds.
 * @param duration Duration struct.
 * @return Milliseconds as int64_t.
 */
int64_t ol_time_to_milliseconds(ol_duration_t duration);

/**
 * @brief Format current time as human-readable string.
 * @param buffer Destination buffer.
 * @param size Buffer size.
 */
void ol_time_format_now(char *buffer, size_t size);

#endif /* OL_TIME_UTILS_H */
