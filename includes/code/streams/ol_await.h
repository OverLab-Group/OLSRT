/**
 * @file ol_await.h
 * @brief Future awaiting utilities for OLSRT
 * @version 1.2.0
 * 
 * This header provides functions for awaiting futures, including cooperative
 * waiting that keeps event loops responsive.
 */

#ifndef OL_AWAIT_H
#define OL_AWAIT_H

#include "ol_common.h"
#include "ol_promise.h"
#include "ol_event_loop.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Await a future until a deadline
 * 
 * Blocks until the future is resolved or the deadline expires.
 * 
 * @param f Future to await (must not be NULL)
 * @param deadline_ns Absolute deadline in nanoseconds (0 for infinite)
 * @return 1 on completion, OL_TIMEOUT on timeout, OL_ERROR on error
 */
OL_API int ol_await_future(ol_future_t *f, int64_t deadline_ns);

/**
 * @brief Await a future cooperatively from an event loop thread
 * 
 * Polls the future in short slices and yields control to the event loop
 * between polls, keeping the loop responsive. This must only be called
 * from the event loop thread.
 * 
 * @param loop Event loop instance (optional)
 * @param f Future to await (must not be NULL)
 * @param deadline_ns Absolute deadline in nanoseconds (0 for infinite)
 * @return 1 on completion, OL_TIMEOUT on timeout, OL_ERROR on error
 * @warning Must only be called from the event loop thread
 */
OL_API int ol_await_future_with_loop(ol_event_loop_t *loop,
                                     ol_future_t *f,
                                     int64_t deadline_ns);

#ifdef __cplusplus
}
#endif

#endif /* OL_AWAIT_H */
