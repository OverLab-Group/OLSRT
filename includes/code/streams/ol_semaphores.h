#ifndef OL_SEMAPHORES_H
#define OL_SEMAPHORES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ol_sem ol_sem_t;

/* Initialize a counting semaphore with initial and maximum counts.
 * For standard semantics, set max_count >= initial and >0.
 */
/**
 * @brief Initialize a counting semaphore.
 *
 * @param s         Semaphore to initialize.
 * @param initial   Initial count.
 * @param max_count Maximum count (must be >= initial and > 0).
 * @return 0 on success, -1 on error.
 * @see ol_sem_destroy
 */

int ol_sem_init(ol_sem_t *s, unsigned int initial, unsigned int max_count);

/* Destroy semaphore and release resources. */
int ol_sem_destroy(ol_sem_t *s);

/* Increment (post) the semaphore by 1.
 * Returns 0 on success; -1 if already at max_count or error.
 */
/**
 * @brief Increment the semaphore by one.
 *
 * @param s Semaphore handle.
 * @return 0 on success, -1 if the count is already at max_count or on
 *         error.
 */

int ol_sem_post(ol_sem_t *s);

/* Try to decrement (wait) without blocking.
 * Returns 1 if acquired, 0 if would-block, -1 on error.
 */
int ol_sem_trywait(ol_sem_t *s);

/* Decrement (wait) with absolute deadline in ns (monotonic).
 * deadline_ns <= 0 means infinite wait.
 * Returns 0 if acquired, -3 on timeout, -1 on error.
 */
/**
 * @brief Decrement with an absolute deadline.
 *
 * @param s           Semaphore handle.
 * @param deadline_ns Absolute monotonic deadline in nanoseconds.
 *                    0 or negative means wait forever.
 * @return 0 on success, -3 on timeout, -1 on error.
 */

int ol_sem_wait_until(ol_sem_t *s, int64_t deadline_ns);

/* Get current count (best-effort, non-atomic snapshot). */
int ol_sem_getvalue(ol_sem_t *s, int *out_value);

#ifdef __cplusplus
}
#endif

#endif /* OL_SEMAPHORES_H */
