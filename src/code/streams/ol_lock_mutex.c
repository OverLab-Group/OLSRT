/**
 * @file ol_lock_mutex.c
 * @brief Cross-platform synchronization primitives: mutex, condition variable, and read-write lock.
 *
 * This file provides thin, consistent wrappers around native OS synchronization
 * primitives for use across the runtime:
 *   - ol_mutex_t: mutual exclusion (blocking + try)
 *   - ol_cond_t: condition variable with absolute-deadline wait
 *   - ol_rwlock_t: read-write lock (shared/exclusive)
 *
 * Supported platforms:
 *   - POSIX: pthread_mutex_t, pthread_cond_t, pthread_rwlock_t
 *   - Windows: CRITICAL_SECTION, CONDITION_VARIABLE, SRWLOCK
 *
 * API conventions:
 *   - init/destroy return 0 on success, -1 on invalid argument or failure.
 *   - lock/unlock return 0 on success, -1 on invalid argument.
 *   - trylock returns 1 on success, 0 if would-block, -1 on error.
 *   - ol_cond_wait_until returns 1 on signaled, 0 on timeout, -1 on error.
 *
 * Timed waits:
 *   - The public API accepts an absolute monotonic deadline in nanoseconds.
 *   - POSIX path converts absolute ns -> struct timespec and calls pthread_cond_timedwait.
 *   - Windows path converts absolute ns -> relative milliseconds and calls SleepConditionVariableCS.
 *
 * Thread-safety:
 *   - These wrappers are thread-safe as long as callers follow usual mutex/cond usage:
 *     lock the mutex before calling wait, etc.
 *
 * Ownership:
 *   - The caller is responsible for allocating ol_mutex_t / ol_cond_t / ol_rwlock_t
 *     storage and for ensuring they are destroyed only when no threads may access them.
 */

#include "ol_lock_mutex.h"

#include <string.h>

#if defined(_WIN32)

    #include <windows.h>
    #include <synchapi.h>

    /**
     * @brief Helper: monotonic-ish current time in milliseconds on Windows.
     *
     * Uses GetTickCount64 which is monotonic for practical purposes (wraps after long time).
     * This is used only to compute relative timeouts for SleepConditionVariableCS.
     *
     * @return Current time in milliseconds.
     */
    static inline int64_t ol_now_ms_win(void) {
        return (int64_t)GetTickCount64();
    }

    /* ---------------------------------------------------------------------
     * Mutex (CRITICAL_SECTION) API - Windows
     * --------------------------------------------------------------------- */

    /**
     * @brief Initialize a mutex.
     *
     * Wraps InitializeCriticalSection. The caller must provide a valid pointer.
     *
     * @param m Pointer to ol_mutex_t to initialize.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_mutex_init(ol_mutex_t *m) {
        if (!m) return -1;
        InitializeCriticalSection(&m->cs);
        return 0;
    }

    /**
     * @brief Destroy a mutex.
     *
     * Wraps DeleteCriticalSection. Behavior is undefined if the mutex is locked by another thread.
     *
     * @param m Pointer to ol_mutex_t to destroy.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_mutex_destroy(ol_mutex_t *m) {
        if (!m) return -1;
        DeleteCriticalSection(&m->cs);
        return 0;
    }

    /**
     * @brief Lock a mutex (blocking).
     *
     * Wraps EnterCriticalSection. Blocks until the lock is acquired.
     *
     * @param m Pointer to ol_mutex_t.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_mutex_lock(ol_mutex_t *m) {
        if (!m) return -1;
        EnterCriticalSection(&m->cs);
        return 0;
    }

    /**
     * @brief Try to lock a mutex without blocking.
     *
     * Wraps TryEnterCriticalSection.
     *
     * @param m Pointer to ol_mutex_t.
     * @return 1 if lock acquired, 0 if would-block, -1 on invalid argument.
     */
    int ol_mutex_trylock(ol_mutex_t *m) {
        if (!m) return -1;
        BOOL ok = TryEnterCriticalSection(&m->cs);
        return ok ? 1 : 0;
    }

    /**
     * @brief Unlock a mutex.
     *
     * Wraps LeaveCriticalSection.
     *
     * @param m Pointer to ol_mutex_t.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_mutex_unlock(ol_mutex_t *m) {
        if (!m) return -1;
        LeaveCriticalSection(&m->cs);
        return 0;
    }

    /* ---------------------------------------------------------------------
     * Condition variable API - Windows
     * --------------------------------------------------------------------- */

    /**
     * @brief Initialize a condition variable.
     *
     * Wraps InitializeConditionVariable. No explicit destroy required on Windows.
     *
     * @param c Pointer to ol_cond_t.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_cond_init(ol_cond_t *c) {
        if (!c) return -1;
        InitializeConditionVariable(&c->cv);
        return 0;
    }

    /**
     * @brief Destroy a condition variable.
     *
     * No-op on Windows (CONDITION_VARIABLE has no destroy).
     *
     * @param c Pointer to ol_cond_t.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_cond_destroy(ol_cond_t *c) {
        (void)c;
        return 0;
    }

    /**
     * @brief Wait on a condition variable until an absolute monotonic deadline.
     *
     * Windows CONDITION_VARIABLE expects a relative timeout in milliseconds.
     * If deadline_ns <= 0, wait indefinitely.
     *
     * Return values:
     *   1 on signaled,
     *   0 on timeout,
     *  -1 on error (invalid args or system error).
     *
     * Notes:
     *   - Caller must hold the associated ol_mutex_t before calling.
     *   - The function converts the absolute monotonic deadline to a relative
     *     millisecond timeout using GetTickCount64. This is a practical approach
     *     but not strictly nanosecond-accurate.
     *
     * @param c Pointer to ol_cond_t.
     * @param m Pointer to ol_mutex_t (must be locked by caller).
     * @param deadline_ns Absolute monotonic deadline in nanoseconds.
     * @return 1/0/-1 as described.
     */
    int ol_cond_wait_until(ol_cond_t *c, ol_mutex_t *m, int64_t deadline_ns) {
        if (!c || !m) return -1;
        if (deadline_ns <= 0) {
            BOOL ok = SleepConditionVariableCS(&c->cv, &m->cs, INFINITE);
            return ok ? 1 : -1;
        }
        int64_t deadline_ms = deadline_ns / 1000000LL;
        int64_t now_ms = ol_now_ms_win();
        DWORD rel_ms = 0;
        if (deadline_ms > now_ms) {
            int64_t diff = deadline_ms - now_ms;
            rel_ms = (diff > 0x7FFFFFFFLL) ? 0x7FFFFFFF : (DWORD)diff;
        } else {
            rel_ms = 0;
        }
        BOOL ok = SleepConditionVariableCS(&c->cv, &m->cs, rel_ms);
        if (ok) return 1;
        return (GetLastError() == ERROR_TIMEOUT) ? 0 : -1;
    }

    /**
     * @brief Signal one waiter on the condition variable.
     *
     * @param c Pointer to ol_cond_t.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_cond_signal(ol_cond_t *c) {
        if (!c) return -1;
        WakeConditionVariable(&c->cv);
        return 0;
    }

    /**
     * @brief Wake all waiters on the condition variable.
     *
     * @param c Pointer to ol_cond_t.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_cond_broadcast(ol_cond_t *c) {
        if (!c) return -1;
        WakeAllConditionVariable(&c->cv);
        return 0;
    }

    /* ---------------------------------------------------------------------
     * Read-write lock (SRWLOCK) API - Windows
     * --------------------------------------------------------------------- */

    /**
     * @brief Initialize a read-write lock.
     *
     * SRWLOCK does not require explicit destruction.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_rwlock_init(ol_rwlock_t *rw) {
        if (!rw) return -1;
        InitializeSRWLock(&rw->rw);
        return 0;
    }

    /**
     * @brief Destroy a read-write lock.
     *
     * No-op for SRWLOCK.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_rwlock_destroy(ol_rwlock_t *rw) {
        (void)rw;
        return 0;
    }

    /**
     * @brief Acquire read lock (shared).
     *
     * Blocks until acquired.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_rwlock_rdlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        AcquireSRWLockShared(&rw->rw);
        return 0;
    }

    /**
     * @brief Try to acquire read lock without blocking.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 1 on success, 0 if would-block, -1 on invalid argument.
     */
    int ol_rwlock_tryrdlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        BOOLEAN ok = TryAcquireSRWLockShared(&rw->rw);
        return ok ? 1 : 0;
    }

    /**
     * @brief Release read lock.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_rwlock_rdunlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        ReleaseSRWLockShared(&rw->rw);
        return 0;
    }

    /**
     * @brief Acquire write lock (exclusive).
     *
     * Blocks until acquired.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_rwlock_wrlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        AcquireSRWLockExclusive(&rw->rw);
        return 0;
    }

    /**
     * @brief Try to acquire write lock without blocking.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 1 on success, 0 if would-block, -1 on invalid argument.
     */
    int ol_rwlock_trywrlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        BOOLEAN ok = TryAcquireSRWLockExclusive(&rw->rw);
        return ok ? 1 : 0;
    }

    /**
     * @brief Release write lock.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 0 on success, -1 on invalid argument.
     */
    int ol_rwlock_wrunlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        ReleaseSRWLockExclusive(&rw->rw);
        return 0;
    }

#else /* POSIX implementation */

    #include <errno.h>
    #include <time.h>
    #include <pthread.h>

    /**
     * @brief Convert absolute nanoseconds to struct timespec for pthread_cond_timedwait.
     *
     * Ensures non-negative fields and normalizes the timespec.
     *
     * @param abs_ns Absolute time in nanoseconds.
     * @param ts Output timespec pointer (must be non-NULL).
     */
    static inline void ol_ns_to_timespec(int64_t abs_ns, struct timespec *ts) {
        ts->tv_sec  = abs_ns / 1000000000LL;
        ts->tv_nsec = abs_ns % 1000000000LL;
        if (ts->tv_nsec < 0) ts->tv_nsec = 0;
        if (ts->tv_sec < 0) ts->tv_sec = 0;
    }

    /* ---------------------------------------------------------------------
     * Mutex (pthread_mutex_t) API - POSIX
     * --------------------------------------------------------------------- */

    /**
     * @brief Initialize a mutex.
     *
     * Uses default attributes. Caller must provide a valid pointer.
     *
     * @param m Pointer to ol_mutex_t.
     * @return 0 on success, -1 on error.
     */
    int ol_mutex_init(ol_mutex_t *m) {
        if (!m) return -1;
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        /* Default (non-recursive) mutex; change attr if recursive behavior is required. */
        int r = pthread_mutex_init(&m->m, &attr);
        pthread_mutexattr_destroy(&attr);
        return (r == 0) ? 0 : -1;
    }

    /**
     * @brief Destroy a mutex.
     *
     * Behavior undefined if mutex is locked by another thread.
     *
     * @param m Pointer to ol_mutex_t.
     * @return 0 on success, -1 on error.
     */
    int ol_mutex_destroy(ol_mutex_t *m) {
        if (!m) return -1;
        return (pthread_mutex_destroy(&m->m) == 0) ? 0 : -1;
    }

    /**
     * @brief Lock a mutex (blocking).
     *
     * @param m Pointer to ol_mutex_t.
     * @return 0 on success, -1 on error.
     */
    int ol_mutex_lock(ol_mutex_t *m) {
        if (!m) return -1;
        return (pthread_mutex_lock(&m->m) == 0) ? 0 : -1;
    }

    /**
     * @brief Try to lock a mutex without blocking.
     *
     * Returns:
     *   1 if lock acquired,
     *   0 if would-block (EBUSY),
     *  -1 on other errors or invalid argument.
     *
     * @param m Pointer to ol_mutex_t.
     * @return 1/0/-1 as described.
     */
    int ol_mutex_trylock(ol_mutex_t *m) {
        if (!m) return -1;
        int r = pthread_mutex_trylock(&m->m);
        if (r == 0) return 1;
        if (r == EBUSY) return 0;
        return -1;
    }

    /**
     * @brief Unlock a mutex.
     *
     * @param m Pointer to ol_mutex_t.
     * @return 0 on success, -1 on error.
     */
    int ol_mutex_unlock(ol_mutex_t *m) {
        if (!m) return -1;
        return (pthread_mutex_unlock(&m->m) == 0) ? 0 : -1;
    }

    /* ---------------------------------------------------------------------
     * Condition variable API - POSIX
     * --------------------------------------------------------------------- */

    /**
     * @brief Initialize a condition variable.
     *
     * Uses default attributes. If CLOCK_MONOTONIC support is desired for condvars,
     * set the condattr accordingly before init (not done here for portability).
     *
     * @param c Pointer to ol_cond_t.
     * @return 0 on success, -1 on error.
     */
    int ol_cond_init(ol_cond_t *c) {
        if (!c) return -1;
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
        int r = pthread_cond_init(&c->c, &attr);
        pthread_condattr_destroy(&attr);
        return (r == 0) ? 0 : -1;
    }

    /**
     * @brief Destroy a condition variable.
     *
     * @param c Pointer to ol_cond_t.
     * @return 0 on success, -1 on error.
     */
    int ol_cond_destroy(ol_cond_t *c) {
        if (!c) return -1;
        return (pthread_cond_destroy(&c->c) == 0) ? 0 : -1;
    }

    /**
     * @brief Wait on a condition variable until an absolute monotonic deadline.
     *
     * If deadline_ns <= 0, waits indefinitely.
     *
     * Return values:
     *   1 on signaled,
     *   0 on timeout (ETIMEDOUT),
     *  -1 on error.
     *
     * Caller must hold the mutex m before calling.
     *
     * @param c Pointer to ol_cond_t.
     * @param m Pointer to ol_mutex_t (must be locked).
     * @param deadline_ns Absolute monotonic deadline in nanoseconds.
     * @return 1/0/-1 as described.
     */
    int ol_cond_wait_until(ol_cond_t *c, ol_mutex_t *m, int64_t deadline_ns) {
        if (!c || !m) return -1;
        if (deadline_ns <= 0) {
            int r = pthread_cond_wait(&c->c, &m->m);
            return (r == 0) ? 1 : -1;
        }
        struct timespec ts;
        ol_ns_to_timespec(deadline_ns, &ts);
        int r = pthread_cond_timedwait(&c->c, &m->m, &ts);
        if (r == 0) return 1;
        if (r == ETIMEDOUT) return 0;
        return -1;
    }

    /**
     * @brief Signal one waiter on the condition variable.
     *
     * @param c Pointer to ol_cond_t.
     * @return 0 on success, -1 on error.
     */
    int ol_cond_signal(ol_cond_t *c) {
        if (!c) return -1;
        return (pthread_cond_signal(&c->c) == 0) ? 0 : -1;
    }

    /**
     * @brief Wake all waiters on the condition variable.
     *
     * @param c Pointer to ol_cond_t.
     * @return 0 on success, -1 on error.
     */
    int ol_cond_broadcast(ol_cond_t *c) {
        if (!c) return -1;
        return (pthread_cond_broadcast(&c->c) == 0) ? 0 : -1;
    }

    /* ---------------------------------------------------------------------
     * Read-write lock (pthread_rwlock_t) API - POSIX
     * --------------------------------------------------------------------- */

    /**
     * @brief Initialize a read-write lock.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 0 on success, -1 on error.
     */
    int ol_rwlock_init(ol_rwlock_t *rw) {
        if (!rw) return -1;
        pthread_rwlockattr_t attr;
        pthread_rwlockattr_init(&attr);
        int r = pthread_rwlock_init(&rw->rw, &attr);
        pthread_rwlockattr_destroy(&attr);
        return (r == 0) ? 0 : -1;
    }

    /**
     * @brief Destroy a read-write lock.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 0 on success, -1 on error.
     */
    int ol_rwlock_destroy(ol_rwlock_t *rw) {
        if (!rw) return -1;
        return (pthread_rwlock_destroy(&rw->rw) == 0) ? 0 : -1;
    }

    /**
     * @brief Acquire read lock (shared).
     *
     * Blocks until acquired.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 0 on success, -1 on error.
     */
    int ol_rwlock_rdlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        return (pthread_rwlock_rdlock(&rw->rw) == 0) ? 0 : -1;
    }

    /**
     * @brief Try to acquire read lock without blocking.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 1 on success, 0 if would-block, -1 on error.
     */
    int ol_rwlock_tryrdlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        int r = pthread_rwlock_tryrdlock(&rw->rw);
        if (r == 0) return 1;
        if (r == EBUSY) return 0;
        return -1;
    }

    /**
     * @brief Release read lock.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 0 on success, -1 on error.
     */
    int ol_rwlock_rdunlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        return (pthread_rwlock_unlock(&rw->rw) == 0) ? 0 : -1;
    }

    /**
     * @brief Acquire write lock (exclusive).
     *
     * Blocks until acquired.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 0 on success, -1 on error.
     */
    int ol_rwlock_wrlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        return (pthread_rwlock_wrlock(&rw->rw) == 0) ? 0 : -1;
    }

    /**
     * @brief Try to acquire write lock without blocking.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 1 on success, 0 if would-block, -1 on error.
     */
    int ol_rwlock_trywrlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        int r = pthread_rwlock_trywrlock(&rw->rw);
        if (r == 0) return 1;
        if (r == EBUSY) return 0;
        return -1;
    }

    /**
     * @brief Release write lock.
     *
     * @param rw Pointer to ol_rwlock_t.
     * @return 0 on success, -1 on error.
     */
    int ol_rwlock_wrunlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        return (pthread_rwlock_unlock(&rw->rw) == 0) ? 0 : -1;
    }

#endif