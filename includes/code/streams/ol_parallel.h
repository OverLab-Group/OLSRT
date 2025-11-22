#ifndef OL_PARALLEL_H
#define OL_PARALLEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ol_parallel_pool ol_parallel_pool_t;

/* Task function signature */
typedef void (*ol_task_fn)(void *arg);

/* Create a pool with 'num_threads' worker threads (>=1).
 * Returns NULL on failure.
 */
ol_parallel_pool_t* ol_parallel_create(size_t num_threads);

/* Destroy the pool; equivalent to shutdown with drain=true then free resources. */
void ol_parallel_destroy(ol_parallel_pool_t *pool);

/* Submit a task to the pool (non-blocking).
 * Returns 0 on success, negative on failure.
 */
int ol_parallel_submit(ol_parallel_pool_t *pool, ol_task_fn fn, void *arg);

/* Wait until the queue is empty and all currently submitted tasks finish. */
int ol_parallel_flush(ol_parallel_pool_t *pool);

/* Shutdown:
 * - If drain==true: stop accepting new tasks, run all queued tasks, then stop workers.
 * - If drain==false: stop accepting new tasks, cancel pending (not-yet-started) tasks, stop workers.
 * Returns 0 on success.
 */
int ol_parallel_shutdown(ol_parallel_pool_t *pool, bool drain);

/* Introspection (best-effort) */
size_t ol_parallel_thread_count(const ol_parallel_pool_t *pool);
size_t ol_parallel_queue_size(const ol_parallel_pool_t *pool);
bool   ol_parallel_is_running(const ol_parallel_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* OL_PARALLEL_H */
