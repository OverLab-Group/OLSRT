#ifndef OL_DEADLINES_H
#define OL_DEADLINES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Deadline represented as absolute time in nanoseconds (monotonic clock). */
typedef struct {
    int64_t when_ns; // absolute time (ns) at which the deadline expires
} ol_deadline_t;

/* Time acquisition (monotonic), conversion and checks */
int64_t ol_monotonic_now_ns(void);

/* Create deadlines */
ol_deadline_t ol_deadline_from_ns(int64_t ns_from_now);
ol_deadline_t ol_deadline_from_ms(int64_t ms_from_now);
ol_deadline_t ol_deadline_from_sec(double seconds);

/* Query */
bool    ol_deadline_expired(ol_deadline_t dl);
int64_t ol_deadline_remaining_ns(ol_deadline_t dl);
int64_t ol_deadline_remaining_ms(ol_deadline_t dl);

/* Sleep helpers: sleep until deadline (interruptible if remaining <= 0) */
void ol_sleep_until(ol_deadline_t dl);

/* Utility: clamp a timeout in milliseconds to a safe poll timeout range. */
int ol_clamp_poll_timeout_ms(int64_t remaining_ms);

#endif /* OL_DEADLINES_H */
