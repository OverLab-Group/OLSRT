/**
 * @file ol_deadlines.c
 * @brief Cross-platform deadline and timeout utilities
 * @version 1.2.0
 * 
 * This module provides monotonic time utilities and deadline management
 * functions that work consistently across Linux, Windows, macOS, and BSD.
 */

#include "ol_deadlines.h"
#include "ol_common.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(OL_PLATFORM_WINDOWS)
    #include <windows.h>
#else
    #include <time.h>
    #include <unistd.h>
#endif

/* --------------------------------------------------------------------------
 * Platform-specific monotonic clock implementation
 * -------------------------------------------------------------------------- */

#if defined(OL_PLATFORM_WINDOWS)

/* Windows implementation using QueryPerformanceCounter */
static int64_t ol_monotonic_now_ns_impl(void) {
    static LARGE_INTEGER frequency = {0};
    LARGE_INTEGER counter;
    
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
        if (frequency.QuadPart == 0) {
            return 0; /* Should never happen */
        }
    }
    
    QueryPerformanceCounter(&counter);
    return (counter.QuadPart * 1000000000LL) / frequency.QuadPart;
}

#elif defined(CLOCK_MONOTONIC)

/* POSIX with CLOCK_MONOTONIC support */
static int64_t ol_monotonic_now_ns_impl(void) {
    struct timespec ts;
    
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        /* Fallback to CLOCK_REALTIME if monotonic is unavailable */
        clock_gettime(CLOCK_REALTIME, &ts);
    }
    
    return ((int64_t)ts.tv_sec * 1000000000LL) + ts.tv_nsec;
}

#else

/* Fallback implementation using gettimeofday (not strictly monotonic) */
#include <sys/time.h>
static int64_t ol_monotonic_now_ns_impl(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000000000LL) + ((int64_t)tv.tv_usec * 1000LL);
}

#endif

/* --------------------------------------------------------------------------
 * Public API implementation
 * -------------------------------------------------------------------------- */

int64_t ol_monotonic_now_ns(void) {
    return ol_monotonic_now_ns_impl();
}

ol_deadline_t ol_deadline_from_ns(int64_t ns_from_now) {
    ol_deadline_t d;
    int64_t now = ol_monotonic_now_ns();
    
    if (ns_from_now <= 0) {
        d.when_ns = now;
    } else {
        /* Handle overflow */
        if (ns_from_now > INT64_MAX - now) {
            d.when_ns = INT64_MAX;
        } else {
            d.when_ns = now + ns_from_now;
        }
    }
    
    return d;
}

ol_deadline_t ol_deadline_from_ms(int64_t ms_from_now) {
    int64_t ns = 0;
    
    if (ms_from_now > 0) {
        if (ms_from_now > (INT64_MAX / 1000000LL)) {
            ns = INT64_MAX;
        } else {
            ns = ms_from_now * 1000000LL;
        }
    }
    
    return ol_deadline_from_ns(ns);
}

ol_deadline_t ol_deadline_from_sec(double seconds) {
    int64_t ns = 0;
    
    if (seconds > 0.0) {
        double ns_double = seconds * 1e9;
        if (ns_double > (double)INT64_MAX) {
            ns = INT64_MAX;
        } else {
            ns = (int64_t)ns_double;
        }
    }
    
    return ol_deadline_from_ns(ns);
}

bool ol_deadline_expired(ol_deadline_t dl) {
    return ol_monotonic_now_ns() >= dl.when_ns;
}

int64_t ol_deadline_remaining_ns(ol_deadline_t dl) {
    int64_t now = ol_monotonic_now_ns();
    int64_t rem = dl.when_ns - now;
    return (rem > 0) ? rem : 0;
}

int64_t ol_deadline_remaining_ms(ol_deadline_t dl) {
    int64_t ns = ol_deadline_remaining_ns(dl);
    return ns / 1000000LL;
}

void ol_sleep_until(ol_deadline_t dl) {
    int64_t rem_ns = ol_deadline_remaining_ns(dl);
    if (rem_ns <= 0) {
        return;
    }
    
#if defined(OL_PLATFORM_WINDOWS)
    /* Windows sleep with high precision */
    int64_t ms = rem_ns / 1000000LL;
    if (ms > 0) {
        Sleep((DWORD)ms);
    }
    
    /* Handle sub-millisecond remainder with busy wait */
    int64_t ns_remainder = rem_ns % 1000000LL;
    if (ns_remainder > 0) {
        LARGE_INTEGER frequency, start, end;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        
        int64_t target_cycles = (ns_remainder * frequency.QuadPart) / 1000000000LL;
        
        do {
            QueryPerformanceCounter(&end);
        } while ((end.QuadPart - start.QuadPart) < target_cycles);
    }
#else
    /* POSIX nanosleep */
    struct timespec req;
    req.tv_sec = rem_ns / 1000000000LL;
    req.tv_nsec = rem_ns % 1000000000LL;
    
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
        /* Restart if interrupted by signal */
        continue;
    }
#endif
}

int ol_clamp_poll_timeout_ms(int64_t remaining_ms) {
    if (remaining_ms <= 0) {
        return 0;
    }
    
    /* Cap at 30 seconds to avoid integer overflow in poll/select APIs */
    if (remaining_ms > 30000) {
        return 30000;
    }
    
    /* Ensure it fits in 32-bit signed int */
    if (remaining_ms > 0x7FFFFFFF) {
        return 0x7FFFFFFF;
    }
    
    return (int)remaining_ms;
}