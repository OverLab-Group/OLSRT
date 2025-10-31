/* OLSRT - Time utilities  */

#include "compat.h"
#include "olsrt.h"

/* Return current wall-clock time (UTC) in milliseconds.
 * Uses gettimeofday on all platforms (shim on Windows provided by compat.h).
 */
uint64_t ol_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

/* Return monotonic time in milliseconds.
 * On POSIX: CLOCK_MONOTONIC if available, otherwise CLOCK_REALTIME.
 * On Windows: compat.h provides clock_gettime shim backed by QPC.
 */
uint64_t ol_monotonic_ms(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        /* Fallback: use realtime if monotonic not available */
        clock_gettime(CLOCK_REALTIME, &ts);
    }
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}
