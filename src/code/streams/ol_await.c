/**
 * @file ol_await.c
 * @brief Future awaiting utilities for OLSRT
 * @version 1.2.0
 * 
 * This module provides functions for awaiting futures, including
 * cooperative waiting that keeps event loops responsive across
 * all supported platforms.
 */

#include "ol_await.h"
#include "ol_common.h"
#include "ol_future.h"
#include "ol_event_loop.h"
#include "ol_deadlines.h"

#include <stdlib.h>
#include <string.h>

#if defined(OL_PLATFORM_WINDOWS)
    #include <windows.h>
#else
    #include <time.h>
    #include <unistd.h>
#endif

/* --------------------------------------------------------------------------
 * Internal helper functions
 * -------------------------------------------------------------------------- */

/**
 * @brief Sleep for a short duration (milliseconds)
 */
static void ol_small_sleep_ms(long ms) {
    if (ms <= 0) {
        return;
    }
    
#if defined(OL_PLATFORM_WINDOWS)
    Sleep((DWORD)ms);
#else
    struct timespec req, rem;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    
    /* Handle interruption by signals */
    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
#endif
}

/* --------------------------------------------------------------------------
 * Public API implementation
 * -------------------------------------------------------------------------- */

int ol_await_future(ol_future_t *f, int64_t deadline_ns) {
    if (!f) {
        return OL_ERROR;
    }
    
    /* Delegate to future's await function */
    return ol_future_await(f, deadline_ns);
}

int ol_await_future_with_loop(ol_event_loop_t *loop,
                              ol_future_t *f,
                              int64_t deadline_ns) {
    if (!f) {
        return OL_ERROR;
    }
    
    const long POLL_SLICE_MS = 10; /* 10ms polling slice */
    int64_t start_ns = ol_monotonic_now_ns();
    bool has_deadline = (deadline_ns > 0);
    
    /* Calculate relative deadline from absolute if provided */
    int64_t relative_deadline_ns = 0;
    if (has_deadline) {
        relative_deadline_ns = deadline_ns - start_ns;
        if (relative_deadline_ns <= 0) {
            return OL_TIMEOUT;
        }
    }
    
    while (1) {
        /* Calculate next slice deadline */
        int64_t slice_deadline_ns = 0;
        if (has_deadline) {
            int64_t elapsed = ol_monotonic_now_ns() - start_ns;
            int64_t remaining = relative_deadline_ns - elapsed;
            
            if (remaining <= 0) {
                return OL_TIMEOUT;
            }
            
            /* Use smaller of remaining time or slice size */
            int64_t slice_ns = POLL_SLICE_MS * 1000000LL;
            slice_deadline_ns = (remaining < slice_ns) ? remaining : slice_ns;
        } else {
            slice_deadline_ns = POLL_SLICE_MS * 1000000LL;
        }
        
        /* Try to await with short timeout */
        int r = ol_future_await(f, ol_monotonic_now_ns() + slice_deadline_ns);
        
        if (r != 0) {
            /* Completed, timed out, or error */
            return r;
        }
        
        /* Not ready yet: yield to event loop */
        if (loop) {
            ol_event_loop_wake(loop);
        }
        
        /* Sleep briefly to allow other tasks to run */
        ol_small_sleep_ms(POLL_SLICE_MS);
        
        /* Check if overall deadline expired */
        if (has_deadline) {
            int64_t now = ol_monotonic_now_ns();
            if (now >= deadline_ns) {
                return OL_TIMEOUT;
            }
        }
    }
    
    /* Unreachable */
    return OL_ERROR;
}