#ifndef OL_PARALLEL_H
#define OL_PARALLEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ol_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ol_parallel_pool ol_parallel_pool_t;

/* Task function signature */
typedef void (*ol_task_fn)(void* arg);

/* Create a pool with 'num_threads' worker threads (>=1).
 * Returns NULL on failure.
 */
/**
 * @brief Create a thread pool.
 *
 * @param num_threads Number of worker threads (>= 1).
 * @return Pool handle, or NULL on failure.
 * @see ol_parallel_destroy, ol_parallel_submit
 */

ol_parallel_pool_t* ol_parallel_create(size_t num_threads);

/* Destroy the pool; equivalent to shutdown with drain=true then free resources. */
/**
 * @brief Destroy a thread pool.
 *
 * @details
 * Equivalent to ol_parallel_shutdown(pool, true) followed by a
 * resource release. Pending tasks are drained before workers exit.
 *
 * @param pool Pool handle; NULL is a no-op.
 */

void ol_parallel_destroy(ol_parallel_pool_t* pool);

/* Submit a task to the pool (non-blocking).
 * Returns 0 on success, negative on failure.
 */
/**
 * @brief Submit a task to the pool.
 *
 * @param pool Pool handle.
 * @param fn   Task function.
 * @param arg  Argument forwarded to @p fn.
 * @return 0 on success, -1 on error, -2 if the pool is not accepting
 *         new work.
 */

int ol_parallel_submit(ol_parallel_pool_t* pool, ol_task_fn fn, void* arg);

/* Wait until the queue is empty and all currently submitted tasks finish. */
/**
 * @brief Wait until the queue is empty and all tasks have finished.
 *
 * @param pool Pool handle.
 * @return 0 on success, -1 on error.
 */

int ol_parallel_flush(ol_parallel_pool_t* pool);

/* Shutdown:
 * - If drain==true: stop accepting new tasks, run all queued tasks, then stop workers.
 * - If drain==false: stop accepting new tasks, cancel pending (not-yet-started) tasks, stop workers.
 * Returns 0 on success.
 */
/**
 * @brief Stop the pool.
 *
 * @param pool  Pool handle.
 * @param drain If true, run all queued tasks before stopping; if
 *              false, discard pending tasks.
 * @return 0 on success, -1 on error.
 */

int ol_parallel_shutdown(ol_parallel_pool_t* pool, bool drain);

/* Introspection (best-effort) */
/**
 * @brief Return the number of worker threads in the pool.
 *
 * @param pool Pool handle.
 * @return Worker count, or 0 if @p pool is NULL.
 */

size_t ol_parallel_thread_count(const ol_parallel_pool_t* pool);
/**
 * @brief Return the current number of queued tasks.
 *
 * @param pool Pool handle.
 * @return Queue size, or 0 if @p pool is NULL.
 */

size_t ol_parallel_queue_size(const ol_parallel_pool_t* pool);
/**
 * @brief Check whether the pool is running.
 *
 * @param pool Pool handle.
 * @return true if the pool is accepting work, false otherwise.
 */

bool ol_parallel_is_running(const ol_parallel_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* OL_PARALLEL_H */
