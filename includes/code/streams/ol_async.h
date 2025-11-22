#ifndef OL_ASYNC_H
#define OL_ASYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_parallel.h"
#include "ol_promise.h"
#include "ol_event_loop.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Task function run on pool thread; should return pointer result (owned by caller) */
typedef void* (*ol_async_task_fn)(void *arg);

/* Schedule a task on thread pool. Returns a future that will be fulfilled with the returned pointer.
 * If fn returns NULL that is treated as a valid value (not an error). Use reject on error via ol_promise_reject
 * inside task if you need to signal an error code.
 */
ol_future_t* ol_async_run(ol_parallel_pool_t *pool, ol_async_task_fn fn, void *arg, ol_value_destructor dtor);

/* Event-loop callback signature for scheduling on the loop.
 * cb receives user arg and a promise pointer it should fulfill/reject before returning,
 * or it can return a result pointer which will be used to fulfill the promise automatically.
 *
 * Two styles are supported:
 * - If cb returns non-NULL, that value will be used to fulfill the promise automatically.
 * - If cb returns NULL but uses the promise pointer to fulfill/reject asynchronously, that's also supported.
 *
 * The promise provided must not be destroyed by the callback; the runtime will destroy it after fulfillment.
 */
typedef void* (*ol_async_loop_fn)(struct ol_event_loop *loop, void *arg, ol_promise_t *promise);

/* Schedule a callback to run on the event loop thread (non-blocking). Returns a future.
 * The callback will run in the loop thread and its return value (or explicit promise fulfill) resolves the future.
 * If scheduling fails (e.g., event loop registration problem), returns NULL.
 */
ol_future_t* ol_async_run_on_loop(ol_event_loop_t *loop, ol_async_loop_fn cb, void *arg, ol_value_destructor dtor);

#ifdef __cplusplus
}
#endif

#endif /* OL_ASYNC_H */
