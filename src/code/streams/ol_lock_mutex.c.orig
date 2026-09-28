/**
 * @file ol_lock_mutex.c
 * @brief Cross-platform synchronization primitives implementation
 * @version 1.2.0
 * 
 * This module implements mutexes, condition variables, and read-write locks
 * that work consistently across Windows, Linux, macOS, and BSD systems.
 */

#include "ol_lock_mutex.h"
#include "ol_common.h"
#include "ol_deadlines.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(OL_PLATFORM_WINDOWS)

/* --------------------------------------------------------------------------
 * Windows implementation (CRITICAL_SECTION, CONDITION_VARIABLE, SRWLOCK)
 * -------------------------------------------------------------------------- */

static int64_t ol_windows_now_ms(void) {
    /* GetTickCount64 is monotonic and wraps every 49.7 days */
    return (int64_t)GetTickCount64();
}

int ol_mutex_init(ol_mutex_t *m) {
    if (!m) return OL_ERROR;
    
    InitializeCriticalSection(&m->cs);
    return OL_SUCCESS;
}

int ol_mutex_destroy(ol_mutex_t *m) {
    if (!m) return OL_ERROR;
    
    DeleteCriticalSection(&m->cs);
    return OL_SUCCESS;
}

int ol_mutex_lock(ol_mutex_t *m) {
    if (!m) return OL_ERROR;
    
    EnterCriticalSection(&m->cs);
    return OL_SUCCESS;
}

int ol_mutex_trylock(ol_mutex_t *m) {
    if (!m) return OL_ERROR;
    
    BOOL ok = TryEnterCriticalSection(&m->cs);
    return ok ? 1 : 0;
}

int ol_mutex_unlock(ol_mutex_t *m) {
    if (!m) return OL_ERROR;
    
    LeaveCriticalSection(&m->cs);
    return OL_SUCCESS;
}

int ol_cond_init(ol_cond_t *c) {
    if (!c) return OL_ERROR;
    
    InitializeConditionVariable(&c->cv);
    return OL_SUCCESS;
}

int ol_cond_destroy(ol_cond_t *c) {
    /* Windows condition variables don't need explicit destruction */
    (void)c;
    return OL_SUCCESS;
}

int ol_cond_wait_until(ol_cond_t *c, ol_mutex_t *m, int64_t deadline_ns) {
    if (!c || !m) return OL_ERROR;
    
    if (deadline_ns <= 0) {
        /* Infinite wait */
        BOOL ok = SleepConditionVariableCS(&c->cv, &m->cs, INFINITE);
        return ok ? 1 : OL_ERROR;
    }
    
    /* Convert absolute deadline to relative milliseconds */
    int64_t deadline_ms = deadline_ns / 1000000LL;
    int64_t now_ms = ol_windows_now_ms();
    DWORD timeout_ms;
    
    if (deadline_ms > now_ms) {
        int64_t diff = deadline_ms - now_ms;
        /* Clamp to DWORD_MAX - 1 to avoid INFINITE */
        if (diff > (int64_t)(0xFFFFFFFF - 1)) {
            timeout_ms = 0xFFFFFFFF - 1;
        } else {
            timeout_ms = (DWORD)diff;
        }
    } else {
        timeout_ms = 0;
    }
    
    BOOL ok = SleepConditionVariableCS(&c->cv, &m->cs, timeout_ms);
    if (!ok) {
        DWORD err = GetLastError();
        return (err == ERROR_TIMEOUT) ? 0 : OL_ERROR;
    }
    
    return 1;
}

int ol_cond_signal(ol_cond_t *c) {
    if (!c) return OL_ERROR;
    
    WakeConditionVariable(&c->cv);
    return OL_SUCCESS;
}

int ol_cond_broadcast(ol_cond_t *c) {
    if (!c) return OL_ERROR;
    
    WakeAllConditionVariable(&c->cv);
    return OL_SUCCESS;
}

int ol_rwlock_init(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    InitializeSRWLock(&rw->rw);
    return OL_SUCCESS;
}

int ol_rwlock_destroy(ol_rwlock_t *rw) {
    /* Windows SRWLOCK doesn't need explicit destruction */
    (void)rw;
    return OL_SUCCESS;
}

int ol_rwlock_rdlock(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    AcquireSRWLockShared(&rw->rw);
    return OL_SUCCESS;
}

int ol_rwlock_tryrdlock(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    BOOLEAN ok = TryAcquireSRWLockShared(&rw->rw);
    return ok ? 1 : 0;
}

int ol_rwlock_rdunlock(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    ReleaseSRWLockShared(&rw->rw);
    return OL_SUCCESS;
}

int ol_rwlock_wrlock(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    AcquireSRWLockExclusive(&rw->rw);
    return OL_SUCCESS;
}

int ol_rwlock_trywrlock(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    BOOLEAN ok = TryAcquireSRWLockExclusive(&rw->rw);
    return ok ? 1 : 0;
}

int ol_rwlock_wrunlock(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    ReleaseSRWLockExclusive(&rw->rw);
    return OL_SUCCESS;
}

#else /* POSIX implementation */

/* --------------------------------------------------------------------------
 * POSIX implementation (pthread_mutex_t, pthread_cond_t, pthread_rwlock_t)
 * -------------------------------------------------------------------------- */

static void ol_ns_to_timespec(int64_t abs_ns, struct timespec *ts) {
    ts->tv_sec = abs_ns / 1000000000LL;
    ts->tv_nsec = abs_ns % 1000000000LL;
    
    /* Normalize timespec */
    if (ts->tv_nsec < 0) {
        ts->tv_nsec += 1000000000LL;
        ts->tv_sec -= 1;
    }
}

int ol_mutex_init(ol_mutex_t *m) {
    if (!m) return OL_ERROR;
    
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    
    /* Use non-recursive mutex by default for performance */
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
    
    int r = pthread_mutex_init(&m->m, &attr);
    pthread_mutexattr_destroy(&attr);
    
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_mutex_destroy(ol_mutex_t *m) {
    if (!m) return OL_ERROR;
    
    int r = pthread_mutex_destroy(&m->m);
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_mutex_lock(ol_mutex_t *m) {
    if (!m) return OL_ERROR;
    
    int r = pthread_mutex_lock(&m->m);
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_mutex_trylock(ol_mutex_t *m) {
    if (!m) return OL_ERROR;
    
    int r = pthread_mutex_trylock(&m->m);
    if (r == 0) return 1;
    if (r == EBUSY) return 0;
    return OL_ERROR;
}

int ol_mutex_unlock(ol_mutex_t *m) {
    if (!m) return OL_ERROR;
    
    int r = pthread_mutex_unlock(&m->m);
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_cond_init(ol_cond_t *c) {
    if (!c) return OL_ERROR;
    
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    
    /* Use monotonic clock if available for condition variables */
#ifdef CLOCK_MONOTONIC
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
    
    int r = pthread_cond_init(&c->c, &attr);
    pthread_condattr_destroy(&attr);
    
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_cond_destroy(ol_cond_t *c) {
    if (!c) return OL_ERROR;
    
    int r = pthread_cond_destroy(&c->c);
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_cond_wait_until(ol_cond_t *c, ol_mutex_t *m, int64_t deadline_ns) {
    if (!c || !m) return OL_ERROR;
    
    if (deadline_ns <= 0) {
        /* Infinite wait */
        int r = pthread_cond_wait(&c->c, &m->m);
        return (r == 0) ? 1 : OL_ERROR;
    }
    
    struct timespec ts;
    ol_ns_to_timespec(deadline_ns, &ts);
    
    int r = pthread_cond_timedwait(&c->c, &m->m, &ts);
    if (r == 0) return 1;
    if (r == ETIMEDOUT) return 0;
    return OL_ERROR;
}

int ol_cond_signal(ol_cond_t *c) {
    if (!c) return OL_ERROR;
    
    int r = pthread_cond_signal(&c->c);
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_cond_broadcast(ol_cond_t *c) {
    if (!c) return OL_ERROR;
    
    int r = pthread_cond_broadcast(&c->c);
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_rwlock_init(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);
    
    int r = pthread_rwlock_init(&rw->rw, &attr);
    pthread_rwlockattr_destroy(&attr);
    
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_rwlock_destroy(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    int r = pthread_rwlock_destroy(&rw->rw);
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_rwlock_rdlock(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    int r = pthread_rwlock_rdlock(&rw->rw);
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_rwlock_tryrdlock(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    int r = pthread_rwlock_tryrdlock(&rw->rw);
    if (r == 0) return 1;
    if (r == EBUSY) return 0;
    return OL_ERROR;
}

int ol_rwlock_rdunlock(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    int r = pthread_rwlock_unlock(&rw->rw);
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_rwlock_wrlock(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    int r = pthread_rwlock_wrlock(&rw->rw);
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

int ol_rwlock_trywrlock(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    int r = pthread_rwlock_trywrlock(&rw->rw);
    if (r == 0) return 1;
    if (r == EBUSY) return 0;
    return OL_ERROR;
}

int ol_rwlock_wrunlock(ol_rwlock_t *rw) {
    if (!rw) return OL_ERROR;
    
    int r = pthread_rwlock_unlock(&rw->rw);
    return (r == 0) ? OL_SUCCESS : OL_ERROR;
}

#endif /* Platform-specific implementations */