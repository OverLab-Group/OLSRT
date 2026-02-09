/**
 * @file ol_deadlines.h
 * @brief Deadline and timeout utilities for OLSRT
 * @version 1.2.0
 * 
 * This header provides monotonic time utilities and deadline management
 * functions for timeout-based APIs.
 */

#ifndef OL_DEADLINES_H
#define OL_DEADLINES_H

#include "ol_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Deadline structure
 * 
 * Represents an absolute deadline in monotonic nanoseconds.
 */
typedef struct {
    int64_t when_ns; /**< Absolute deadline in nanoseconds */
} ol_deadline_t;

/**
 * @brief Get current monotonic time in nanoseconds
 * 
 * Uses CLOCK_MONOTONIC on POSIX systems and QueryPerformanceCounter
 * on Windows. The time source is guaranteed to be monotonic.
 * 
 * @return Current monotonic time in nanoseconds
 */
OL_API int64_t ol_monotonic_now_ns(void);

/**
 * @brief Create deadline from nanoseconds offset
 * 
 * @param ns_from_now Offset from now in nanoseconds
 * @return Deadline structure
 */
OL_API ol_deadline_t ol_deadline_from_ns(int64_t ns_from_now);

/**
 * @brief Create deadline from milliseconds offset
 * 
 * @param ms_from_now Offset from now in milliseconds
 * @return Deadline structure
 */
OL_API ol_deadline_t ol_deadline_from_ms(int64_t ms_from_now);

/**
 * @brief Create deadline from seconds offset
 * 
 * @param seconds Offset from now in seconds (fractional allowed)
 * @return Deadline structure
 */
OL_API ol_deadline_t ol_deadline_from_sec(double seconds);

/**
 * @brief Check if deadline has expired
 * 
 * @param dl Deadline to check
 * @return true if expired, false otherwise
 */
OL_API bool ol_deadline_expired(ol_deadline_t dl);

/**
 * @brief Get remaining time until deadline in nanoseconds
 * 
 * @param dl Deadline to check
 * @return Remaining time in nanoseconds (0 if expired)
 */
OL_API int64_t ol_deadline_remaining_ns(ol_deadline_t dl);

/**
 * @brief Get remaining time until deadline in milliseconds
 * 
 * @param dl Deadline to check
 * @return Remaining time in milliseconds (0 if expired)
 */
OL_API int64_t ol_deadline_remaining_ms(ol_deadline_t dl);

/**
 * @brief Sleep until a deadline
 * 
 * Best-effort sleep that may wake early due to signals.
 * 
 * @param dl Deadline to sleep until
 */
OL_API void ol_sleep_until(ol_deadline_t dl);

/**
 * @brief Clamp poll timeout to safe integer range
 * 
 * Used to convert 64-bit timeouts to bounded int values for APIs
 * like poll/select that accept int timeouts.
 * 
 * @param remaining_ms Timeout in milliseconds
 * @return Clamped timeout in milliseconds
 */
OL_API int ol_clamp_poll_timeout_ms(int64_t remaining_ms);

#ifdef __cplusplus
}
#endif

#endif /* OL_DEADLINES_H */