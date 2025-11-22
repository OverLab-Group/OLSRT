#include "ol_lock_mutex.h"

#include <string.h>

/* Platform selection */
#if defined(_WIN32)
    #include <synchapi.h>

    /* Helper: get monotonic-ish current ms for relative timeout.
     * Windows CV APIs require relative timeout. We use GetTickCount64.
     * (Not strictly monotonic in ns, but good enough cross-platform fallback.)
     */
    static inline int64_t ol_now_ms_win(void) {
        return (int64_t)GetTickCount64();
    }

    int ol_mutex_init(ol_mutex_t *m) {
        if (!m) return -1;
        InitializeCriticalSection(&m->cs);
        return 0;
    }
    int ol_mutex_destroy(ol_mutex_t *m) {
        if (!m) return -1;
        DeleteCriticalSection(&m->cs);
        return 0;
    }
    int ol_mutex_lock(ol_mutex_t *m) {
        if (!m) return -1;
        EnterCriticalSection(&m->cs);
        return 0;
    }
    int ol_mutex_trylock(ol_mutex_t *m) {
        if (!m) return -1;
        BOOL ok = TryEnterCriticalSection(&m->cs);
        return ok ? 1 : 0;
    }
    int ol_mutex_unlock(ol_mutex_t *m) {
        if (!m) return -1;
        LeaveCriticalSection(&m->cs);
        return 0;
    }

    int ol_cond_init(ol_cond_t *c) {
        if (!c) return -1;
        InitializeConditionVariable(&c->cv);
        return 0;
    }
    int ol_cond_destroy(ol_cond_t *c) {
        (void)c; /* no-op for Windows CONDITION_VARIABLE */
        return 0;
    }
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
            rel_ms = (diff > 0xFFFFFFFFLL) ? 0xFFFFFFFF : (DWORD)diff;
        } else {
            rel_ms = 0;
        }
        BOOL ok = SleepConditionVariableCS(&c->cv, &m->cs, rel_ms);
        if (ok) return 1;
        return (GetLastError() == ERROR_TIMEOUT) ? 0 : -1;
    }
    int ol_cond_signal(ol_cond_t *c) {
        if (!c) return -1;
        WakeConditionVariable(&c->cv);
        return 0;
    }
    int ol_cond_broadcast(ol_cond_t *c) {
        if (!c) return -1;
        WakeAllConditionVariable(&c->cv);
        return 0;
    }

    int ol_rwlock_init(ol_rwlock_t *rw) {
        if (!rw) return -1;
        InitializeSRWLock(&rw->rw);
        return 0;
    }
    int ol_rwlock_destroy(ol_rwlock_t *rw) {
        (void)rw; /* SRWLOCK has no destroy */
        return 0;
    }
    int ol_rwlock_rdlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        AcquireSRWLockShared(&rw->rw);
        return 0;
    }
    int ol_rwlock_tryrdlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        BOOLEAN ok = TryAcquireSRWLockShared(&rw->rw);
        return ok ? 1 : 0;
    }
    int ol_rwlock_rdunlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        ReleaseSRWLockShared(&rw->rw);
        return 0;
    }
    int ol_rwlock_wrlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        AcquireSRWLockExclusive(&rw->rw);
        return 0;
    }
    int ol_rwlock_trywrlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        BOOLEAN ok = TryAcquireSRWLockExclusive(&rw->rw);
        return ok ? 1 : 0;
    }
    int ol_rwlock_wrunlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        ReleaseSRWLockExclusive(&rw->rw);
        return 0;
    }

#else
    /* POSIX */
    #include <errno.h>
    #include <time.h>

    /* Convert absolute ns to struct timespec for pthread timed waits */
    static inline void ol_ns_to_timespec(int64_t abs_ns, struct timespec *ts) {
        ts->tv_sec  = abs_ns / 1000000000LL;
        ts->tv_nsec = abs_ns % 1000000000LL;
        if (ts->tv_nsec < 0) ts->tv_nsec = 0;
        if (ts->tv_sec < 0) ts->tv_sec = 0;
    }

    int ol_mutex_init(ol_mutex_t *m) {
        if (!m) return -1;
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        /* Non-recursive, error-checking could be enabled if desired */
        int r = pthread_mutex_init(&m->m, &attr);
        pthread_mutexattr_destroy(&attr);
        return (r == 0) ? 0 : -1;
    }
    int ol_mutex_destroy(ol_mutex_t *m) {
        if (!m) return -1;
        return (pthread_mutex_destroy(&m->m) == 0) ? 0 : -1;
    }
    int ol_mutex_lock(ol_mutex_t *m) {
        if (!m) return -1;
        return (pthread_mutex_lock(&m->m) == 0) ? 0 : -1;
    }
    int ol_mutex_trylock(ol_mutex_t *m) {
        if (!m) return -1;
        int r = pthread_mutex_trylock(&m->m);
        if (r == 0) return 1;
        if (r == EBUSY) return 0;
        return -1;
    }
    int ol_mutex_unlock(ol_mutex_t *m) {
        if (!m) return -1;
        return (pthread_mutex_unlock(&m->m) == 0) ? 0 : -1;
    }

    int ol_cond_init(ol_cond_t *c) {
        if (!c) return -1;
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
        /* Use default clock; many libcs use CLOCK_REALTIME.
         * If CLOCK_MONOTONIC is supported for condvars, it would be preferable.
         */
        int r = pthread_cond_init(&c->c, &attr);
        pthread_condattr_destroy(&attr);
        return (r == 0) ? 0 : -1;
    }
    int ol_cond_destroy(ol_cond_t *c) {
        if (!c) return -1;
        return (pthread_cond_destroy(&c->c) == 0) ? 0 : -1;
    }
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
    int ol_cond_signal(ol_cond_t *c) {
        if (!c) return -1;
        return (pthread_cond_signal(&c->c) == 0) ? 0 : -1;
    }
    int ol_cond_broadcast(ol_cond_t *c) {
        if (!c) return -1;
        return (pthread_cond_broadcast(&c->c) == 0) ? 0 : -1;
    }

    int ol_rwlock_init(ol_rwlock_t *rw) {
        if (!rw) return -1;
        pthread_rwlockattr_t attr;
        pthread_rwlockattr_init(&attr);
        int r = pthread_rwlock_init(&rw->rw, &attr);
        pthread_rwlockattr_destroy(&attr);
        return (r == 0) ? 0 : -1;
    }
    int ol_rwlock_destroy(ol_rwlock_t *rw) {
        if (!rw) return -1;
        return (pthread_rwlock_destroy(&rw->rw) == 0) ? 0 : -1;
    }
    int ol_rwlock_rdlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        return (pthread_rwlock_rdlock(&rw->rw) == 0) ? 0 : -1;
    }
    int ol_rwlock_tryrdlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        int r = pthread_rwlock_tryrdlock(&rw->rw);
        if (r == 0) return 1;
        if (r == EBUSY) return 0;
        return -1;
    }
    int ol_rwlock_rdunlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        return (pthread_rwlock_unlock(&rw->rw) == 0) ? 0 : -1;
    }
    int ol_rwlock_wrlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        return (pthread_rwlock_wrlock(&rw->rw) == 0) ? 0 : -1;
    }
    int ol_rwlock_trywrlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        int r = pthread_rwlock_trywrlock(&rw->rw);
        if (r == 0) return 1;
        if (r == EBUSY) return 0;
        return -1;
    }
    int ol_rwlock_wrunlock(ol_rwlock_t *rw) {
        if (!rw) return -1;
        return (pthread_rwlock_unlock(&rw->rw) == 0) ? 0 : -1;
    }
#endif
