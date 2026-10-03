#include "ol_semaphores.h"

#include <string.h>

#if defined(_WIN32)
/* --------------------------- Windows implementation --------------------------- */
#include <windows.h>

int ol_sem_init(ol_sem_t* s, unsigned int initial, unsigned int max_count) {
    if (!s || max_count == 0 || initial > max_count)
        return -1;
    HANDLE h = CreateSemaphoreW(NULL, (LONG)initial, (LONG)max_count, NULL);
    if (!h)
        return -1;
    s->h = h;
    s->max_count = max_count;
    return 0;
}

int ol_sem_destroy(ol_sem_t* s) {
    if (!s || !s->h)
        return -1;
    BOOL ok = CloseHandle(s->h);
    s->h = NULL;
    return ok ? 0 : -1;
}

int ol_sem_post(ol_sem_t* s) {
    if (!s || !s->h)
        return -1;
    /* Windows doesn't expose current count directly; ReleaseSemaphore fails if beyond max. */
    return ReleaseSemaphore(s->h, 1, NULL) ? 0 : -1;
}

int ol_sem_trywait(ol_sem_t* s) {
    if (!s || !s->h)
        return -1;
    DWORD r = WaitForSingleObject(s->h, 0);
    if (r == WAIT_OBJECT_0)
        return 1; /* acquired */
    if (r == WAIT_TIMEOUT)
        return 0; /* would-block */
    return -1;
}

int ol_sem_wait_until(ol_sem_t* s, int64_t deadline_ns) {
    if (!s || !s->h)
        return -1;
    DWORD timeout_ms;
    if (deadline_ns <= 0) {
        timeout_ms = INFINITE;
    } else {
        /* Convert absolute ns to relative ms using GetTickCount64 */
        int64_t now_ms = (int64_t)GetTickCount64();
        int64_t deadline_ms = deadline_ns / 1000000LL;
        int64_t rel = (deadline_ms > now_ms) ? (deadline_ms - now_ms) : 0;
        timeout_ms = (rel > 0xFFFFFFFFLL) ? 0xFFFFFFFFu : (DWORD)rel;
    }
    DWORD r = WaitForSingleObject(s->h, timeout_ms);
    if (r == WAIT_OBJECT_0)
        return 0; /* acquired */
    if (r == WAIT_TIMEOUT)
        return -3; /* timeout */
    return -1;
}

int ol_sem_getvalue(ol_sem_t* s, int* out_value) {
    if (!s || !s->h || !out_value)
        return -1;
    /* Best-effort: try non-blocking acquire to read count indirectly, then release back. */
    DWORD r = WaitForSingleObject(s->h, 0);
    if (r == WAIT_OBJECT_0) {
        /* We acquired one; release back and report "at least 1" */
        (void)ReleaseSemaphore(s->h, 1, NULL);
        *out_value = 1;
        return 0;
    } else if (r == WAIT_TIMEOUT) {
        *out_value = 0;
        return 0;
    }
    return -1;
}

/* --------------------------- End Windows --------------------------- */

#else
/* --------------------------- POSIX implementation --------------------------- */

#include <semaphore.h>
#include <errno.h>
#include <time.h>

/* Convert absolute ns to struct timespec (CLOCK_REALTIME expected by sem_timedwait on many libcs).
 * Note: Some libcs support CLOCK_MONOTONIC for sem_timedwait via sem_clockwait (GNU extension).
 * Here we use CLOCK_REALTIME for portability; deadline_ns should be derived accordingly if needed.
 */
static inline void ns_to_timespec(int64_t abs_ns, struct timespec* ts) {
    ts->tv_sec = abs_ns / 1000000000LL;
    ts->tv_nsec = abs_ns % 1000000000LL;
    if (ts->tv_sec < 0)
        ts->tv_sec = 0;
    if (ts->tv_nsec < 0)
        ts->tv_nsec = 0;
}

int ol_sem_init(ol_sem_t* s, unsigned int initial, unsigned int max_count) {
    if (!s || max_count == 0 || initial > max_count)
        return -1;
    /* pshared = 0 (thread-local process) */
    if (sem_init(&s->sem, 0, initial) != 0)
        return -1;
    s->max_count = max_count;
    return 0;
}

int ol_sem_destroy(ol_sem_t* s) {
    if (!s)
        return -1;
    return (sem_destroy(&s->sem) == 0) ? 0 : -1;
}

int ol_sem_post(ol_sem_t* s) {
    if (!s)
        return -1;
    /* POSIX has no max cap beyond initialization; just post. */
    return (sem_post(&s->sem) == 0) ? 0 : -1;
}

int ol_sem_trywait(ol_sem_t* s) {
    if (!s)
        return -1;
    int r = sem_trywait(&s->sem);
    if (r == 0)
        return 1; /* acquired */
    if (errno == EAGAIN)
        return 0; /* would-block */
    return -1;
}

int ol_sem_wait_until(ol_sem_t* s, int64_t deadline_ns) {
    if (!s)
        return -1;
    if (deadline_ns <= 0) {
        int r = sem_wait(&s->sem);
        return (r == 0) ? 0 : -1;
    }

    /* v1.3.2 fix: convert the monotonic deadline to an absolute
     * CLOCK_REALTIME time.
     *
     * The deadline comes from ol_deadline_from_*(), which is based
     * on CLOCK_MONOTONIC. sem_timedwait() expects CLOCK_REALTIME on
     * every libc without sem_clockwait. Passing a monotonic value
     * made the timeout fire immediately: monotonic time is small
     * (boot-relative), which converts to a 1970 timestamp in the
     * past, so every wait expired at once.
     *
     * Read CLOCK_MONOTONIC and CLOCK_REALTIME, compute the relative
     * interval, add it to realtime. */
    struct timespec mono_ts;
    if (clock_gettime(CLOCK_MONOTONIC, &mono_ts) != 0)
        return -1;
    int64_t now_mono_ns = (int64_t)mono_ts.tv_sec * 1000000000LL
                        + (int64_t)mono_ts.tv_nsec;

    int64_t rel_ns = deadline_ns - now_mono_ns;
    if (rel_ns <= 0)
        return -3;

    struct timespec real_ts;
    if (clock_gettime(CLOCK_REALTIME, &real_ts) != 0)
        return -1;
    int64_t abs_real_ns = (int64_t)real_ts.tv_sec * 1000000000LL
                        + (int64_t)real_ts.tv_nsec + rel_ns;

    struct timespec ts;
    ts.tv_sec  = abs_real_ns / 1000000000LL;
    ts.tv_nsec = abs_real_ns % 1000000000LL;

    int r = sem_timedwait(&s->sem, &ts);
    if (r == 0)
        return 0;
    if (errno == ETIMEDOUT)
        return -3;
    return -1;
}

int ol_sem_getvalue(ol_sem_t* s, int* out_value) {
    if (!s || !out_value)
        return -1;
    int val = 0;
    if (sem_getvalue(&s->sem, &val) != 0)
        return -1;
    *out_value = val;
    return 0;
}

/* --------------------------- End POSIX --------------------------- */
#endif
