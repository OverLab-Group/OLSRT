#ifndef OL_AWAIT_H
#define OL_AWAIT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_promise.h"
#include "ol_event_loop.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Blocking await wrapper: returns 1 completed, 0 timeout, -1 error. */
int ol_await_future(ol_future_t *f, int64_t deadline_ns);

/* Await but keep the event loop processing: call this from the event-loop thread.
 * Behavior: repeatedly wait on future with short timeout slices and call ol_event_loop_run_once
 * if such helper exists. Because ol_event_loop_run does not provide a run-once API in this codebase,
 * we implement cooperative loop pumping by waking the loop and sleeping briefly.
 *
 * Returns same codes as ol_await_future.
 */
int ol_await_future_with_loop(ol_event_loop_t *loop, ol_future_t *f, int64_t deadline_ns);

#ifdef __cplusplus
}
#endif

#endif /* OL_AWAIT_H */
