/**
 * @file ol_async.h
 * @brief Asynchronous task execution primitives for OLSRT
 * @version 1.2.0
 * 
 * This header provides APIs for running tasks on thread pools and event loops,
 * with future/promise semantics for handling asynchronous results.
 */

#ifndef OL_ASYNC_H
#define OL_ASYNC_H

#include "ol_common.h"
#include "ol_promise.h"
#include "ol_event_loop.h"
#include "ol_parallel_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task function type for pool execution
 * @param arg User-provided argument
 * @return Pointer to result (ownership transferred to promise)
 */
typedef void* (*ol_async_task_fn)(void *arg);

/**
 * @brief Loop callback function type
 * @param loop Event loop instance
 * @param arg User-provided argument
 * @param promise Promise to resolve (optional)
 * @return Pointer to result if available, NULL if promise will be resolved later
 */
typedef void* (*ol_async_loop_fn)(ol_event_loop_t *loop, void *arg, ol_promise_t *promise);

/**
 * @brief Run a task on a parallel thread pool
 * 
 * Submits a task to the thread pool and returns a future that will be
 * resolved when the task completes. The task runs on a worker thread
 * and should not block the calling thread.
 * 
 * @param pool Thread pool instance (must not be NULL)
 * @param fn Task function to execute (must not be NULL)
 * @param arg Argument passed to the task function
 * @param dtor Destructor for the result value (optional)
 * @return Future handle, or NULL on error
 * @note The caller must destroy the returned future with ol_future_destroy()
 */
OL_API ol_future_t* ol_async_run(ol_parallel_pool_t *pool,
                                 ol_async_task_fn fn,
                                 void *arg,
                                 ol_value_destructor dtor);

/**
 * @brief Schedule a callback on an event loop thread
 * 
 * Schedules a callback to run on the event loop's thread and returns a
 * future for its result. The callback can either return a result immediately
 * or use the provided promise to resolve asynchronously.
 * 
 * @param loop Event loop instance (must not be NULL)
 * @param cb Callback to execute on loop thread (must not be NULL)
 * @param arg Argument passed to callback
 * @param dtor Destructor for the result value (optional)
 * @return Future handle, or NULL on error
 * @note The caller must destroy the returned future with ol_future_destroy()
 */
OL_API ol_future_t* ol_async_run_on_loop(ol_event_loop_t *loop,
                                         ol_async_loop_fn cb,
                                         void *arg,
                                         ol_value_destructor dtor);

#ifdef __cplusplus
}
#endif

#endif /* OL_ASYNC_H */