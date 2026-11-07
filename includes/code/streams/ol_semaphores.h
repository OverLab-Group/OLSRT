#ifndef OL_SEMAPHORES_H
#define OL_SEMAPHORES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * v1.3.2: ol_sem_t is a complete type.
 *
 * The previous header declared it as an incomplete typedef while
 * ol_sem_init asked the caller to provide storage. Callers had no
 * way to know the struct size, so the type was unusable on the
 * stack. The struct is now fully defined here; the implementation
 * no longer redeclares it.
 */

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
typedef struct {
    HANDLE h;               /* Win32 semaphore handle */
    unsigned int max_count; /* advisory upper bound */
} ol_sem_t;
#else
#include <semaphore.h>
typedef struct {
    sem_t sem;              /* POSIX counting semaphore */
    unsigned int max_count; /* advisory upper bound */
} ol_sem_t;
#endif

/**
 * @brief Initialize a counting semaphore.
 *
 * @param s         Semaphore storage (caller-allocated).
 * @param initial   Initial count (0 <= initial <= max_count).
 * @param max_count Maximum count. Must be > 0.
 * @return 0 on success, -1 on invalid arguments or OS error.
 */
int ol_sem_init(ol_sem_t* s, unsigned int initial, unsigned int max_count);

/**
 * @brief Destroy a semaphore.
 *
 * @param s Semaphore to destroy.
 * @return 0 on success, -1 on error.
 */
int ol_sem_destroy(ol_sem_t* s);

/**
 * @brief Increment the semaphore count.
 *
 * @param s Semaphore to post.
 * @return 0 on success, -1 on error (e.g. max_count exceeded).
 */
int ol_sem_post(ol_sem_t* s);

/**
 * @brief Try to decrement the semaphore count without blocking.
 *
 * @param s Semaphore to try.
 * @return 1 if acquired, 0 if would block, -1 on error.
 */
int ol_sem_trywait(ol_sem_t* s);

/**
 * @brief Wait until the semaphore is acquired or the deadline expires.
 *
 * @param s           Semaphore to wait on.
 * @param deadline_ns Absolute monotonic deadline in nanoseconds
 *                    (0 means infinite).
 * @return 0 on acquisition, -3 on timeout, -1 on error.
 */
int ol_sem_wait_until(ol_sem_t* s, int64_t deadline_ns);

/**
 * @brief Read the current semaphore count.
 *
 * @param s         Semaphore to query.
 * @param out_value Output for the current count.
 * @return 0 on success, -1 on error.
 */
int ol_sem_getvalue(ol_sem_t* s, int* out_value);

#ifdef __cplusplus
}
#endif

#endif /* OL_SEMAPHORES_H */
