#ifndef OL_LOCK_MUTEX_H
#define OL_LOCK_MUTEX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#include <windows.h>
typedef struct ol_mutex {
    CRITICAL_SECTION m;
} ol_mutex_t;
typedef struct ol_cond {
    CONDITION_VARIABLE c;
} ol_cond_t;
typedef struct ol_rwlock {
    SRWLOCK rw;
} ol_rwlock_t;
#else
#include <pthread.h>
typedef struct ol_mutex {
    pthread_mutex_t m;
} ol_mutex_t;
typedef struct ol_cond {
    pthread_cond_t c;
} ol_cond_t;
typedef struct ol_rwlock {
 pthread_rwlock_t rw;
} ol_rwlock_t;
#endif

/* -------------------------------
 * Mutex
 * ------------------------------- */
int ol_mutex_init(ol_mutex_t *m);
int ol_mutex_destroy(ol_mutex_t *m);
int ol_mutex_lock(ol_mutex_t *m);
int ol_mutex_trylock(ol_mutex_t *m);   /* returns 1 if acquired, 0 if not, negative on error */
int ol_mutex_unlock(ol_mutex_t *m);

/* -------------------------------
 * Condition Variable
 * ------------------------------- */
/* Initialize/destroy */
int ol_cond_init(ol_cond_t *c);
int ol_cond_destroy(ol_cond_t *c);

/* Wait until notified or deadline passes.
 * - If deadline_ns <= 0: wait indefinitely.
 * - Returns 1 if signaled, 0 if timed out, negative on error.
 */
int ol_cond_wait_until(ol_cond_t *c, ol_mutex_t *m, int64_t deadline_ns);

/* Wake one/all */
int ol_cond_signal(ol_cond_t *c);
int ol_cond_broadcast(ol_cond_t *c);

/* -------------------------------
 * Reader/Writer Lock
 * ------------------------------- */
int ol_rwlock_init(ol_rwlock_t *rw);
int ol_rwlock_destroy(ol_rwlock_t *rw);

int ol_rwlock_rdlock(ol_rwlock_t *rw);
int ol_rwlock_tryrdlock(ol_rwlock_t *rw); /* returns 1 if acquired, 0 otherwise, negative on error */
int ol_rwlock_rdunlock(ol_rwlock_t *rw);

int ol_rwlock_wrlock(ol_rwlock_t *rw);
int ol_rwlock_trywrlock(ol_rwlock_t *rw); /* returns 1 if acquired, 0 otherwise, negative on error */
int ol_rwlock_wrunlock(ol_rwlock_t *rw);

#ifdef __cplusplus
}
#endif

#endif /* OL_LOCK_MUTEX_H */
