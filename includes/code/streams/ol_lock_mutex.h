/**
 * @file ol_lock_mutex.h
 * @brief Cross-platform synchronization primitives
 * @version 1.2.0
 * 
 * This header provides thin wrappers around native OS synchronization
 * primitives: mutexes, condition variables, and read-write locks.
 */

#ifndef OL_LOCK_MUTEX_H
#define OL_LOCK_MUTEX_H

#include "ol_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(OL_PLATFORM_WINDOWS)
    /* Windows implementation using CRITICAL_SECTION and CONDITION_VARIABLE */
    #include <windows.h>
    
    /** @brief Mutex type (Windows CRITICAL_SECTION) */
    typedef struct {
        CRITICAL_SECTION cs;
    } ol_mutex_t;
    
    /** @brief Condition variable type (Windows CONDITION_VARIABLE) */
    typedef struct {
        CONDITION_VARIABLE cv;
    } ol_cond_t;
    
    /** @brief Read-write lock type (Windows SRWLOCK) */
    typedef struct {
        SRWLOCK rw;
    } ol_rwlock_t;
#else
    /* POSIX implementation using pthreads */
    #include <pthread.h>
    
    /** @brief Mutex type (POSIX pthread_mutex_t) */
    typedef struct {
        pthread_mutex_t m;
    } ol_mutex_t;
    
    /** @brief Condition variable type (POSIX pthread_cond_t) */
    typedef struct {
        pthread_cond_t c;
    } ol_cond_t;
    
    /** @brief Read-write lock type (POSIX pthread_rwlock_t) */
    typedef struct {
        pthread_rwlock_t rw;
    } ol_rwlock_t;
#endif

/**
 * @brief Initialize a mutex
 * 
 * @param m Mutex to initialize
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_mutex_init(ol_mutex_t *m);

/**
 * @brief Destroy a mutex
 * 
 * @param m Mutex to destroy
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_mutex_destroy(ol_mutex_t *m);

/**
 * @brief Lock a mutex (blocking)
 * 
 * @param m Mutex to lock
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_mutex_lock(ol_mutex_t *m);

/**
 * @brief Try to lock a mutex (non-blocking)
 * 
 * @param m Mutex to lock
 * @return 1 if locked, 0 if would block, OL_ERROR on error
 */
OL_API int ol_mutex_trylock(ol_mutex_t *m);

/**
 * @brief Unlock a mutex
 * 
 * @param m Mutex to unlock
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_mutex_unlock(ol_mutex_t *m);

/**
 * @brief Initialize a condition variable
 * 
 * @param c Condition variable to initialize
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_cond_init(ol_cond_t *c);

/**
 * @brief Destroy a condition variable
 * 
 * @param c Condition variable to destroy
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_cond_destroy(ol_cond_t *c);

/**
 * @brief Wait on condition variable until deadline
 * 
 * @param c Condition variable
 * @param m Mutex (must be locked by caller)
 * @param deadline_ns Absolute deadline in nanoseconds (0 for infinite)
 * @return 1 if signaled, 0 if timeout, OL_ERROR on error
 */
OL_API int ol_cond_wait_until(ol_cond_t *c,
                              ol_mutex_t *m,
                              int64_t deadline_ns);

/**
 * @brief Signal one waiter on condition variable
 * 
 * @param c Condition variable
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_cond_signal(ol_cond_t *c);

/**
 * @brief Broadcast to all waiters on condition variable
 * 
 * @param c Condition variable
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_cond_broadcast(ol_cond_t *c);

/**
 * @brief Initialize a read-write lock
 * 
 * @param rw Read-write lock to initialize
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_rwlock_init(ol_rwlock_t *rw);

/**
 * @brief Destroy a read-write lock
 * 
 * @param rw Read-write lock to destroy
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_rwlock_destroy(ol_rwlock_t *rw);

/**
 * @brief Acquire read lock (shared)
 * 
 * @param rw Read-write lock
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_rwlock_rdlock(ol_rwlock_t *rw);

/**
 * @brief Try to acquire read lock (non-blocking)
 * 
 * @param rw Read-write lock
 * @return 1 if locked, 0 if would block, OL_ERROR on error
 */
OL_API int ol_rwlock_tryrdlock(ol_rwlock_t *rw);

/**
 * @brief Release read lock
 * 
 * @param rw Read-write lock
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_rwlock_rdunlock(ol_rwlock_t *rw);

/**
 * @brief Acquire write lock (exclusive)
 * 
 * @param rw Read-write lock
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_rwlock_wrlock(ol_rwlock_t *rw);

/**
 * @brief Try to acquire write lock (non-blocking)
 * 
 * @param rw Read-write lock
 * @return 1 if locked, 0 if would block, OL_ERROR on error
 */
OL_API int ol_rwlock_trywrlock(ol_rwlock_t *rw);

/**
 * @brief Release write lock
 * 
 * @param rw Read-write lock
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_rwlock_wrunlock(ol_rwlock_t *rw);

#ifdef __cplusplus
}
#endif

#endif /* OL_LOCK_MUTEX_H */