#ifndef OL_PROMISE_H
#define OL_PROMISE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_lock_mutex.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OL_PROMISE_PENDING = 0,
    OL_PROMISE_FULFILLED,
    OL_PROMISE_REJECTED,
    OL_PROMISE_CANCELED
} ol_promise_state_t;

/* Opaque types */
typedef struct ol_promise ol_promise_t;
typedef struct ol_future  ol_future_t;

/* Optional destructor for stored values */
typedef void (*ol_value_destructor)(void *ptr);

/* Continuation callback signature */
typedef void (*ol_future_cb)(
    struct ol_event_loop *loop,
    ol_promise_state_t state,
    const void *value,
    int error_code,
    void *user_data
);

/* Create a promise; optionally bind to an event loop for wake-ups on resolution. */
ol_promise_t* ol_promise_create(struct ol_event_loop *loop);

/* Destroy promise handle (decrements shared core refcount). */
void ol_promise_destroy(ol_promise_t *p);

/* Get a future for this promise (increments core refcount). */
ol_future_t* ol_promise_get_future(ol_promise_t *p);

/* Resolve operations (first call wins; subsequent calls fail with -1). */
int ol_promise_fulfill(ol_promise_t *p, void *value, ol_value_destructor dtor);
int ol_promise_reject(ol_promise_t *p, int error_code);
int ol_promise_cancel(ol_promise_t *p);

/* Introspection (non-blocking). */
ol_promise_state_t ol_promise_state(const ol_promise_t *p);
bool               ol_promise_is_done(const ol_promise_t *p);

/* Future API */

/* Destroy future handle (decrements shared core refcount). */
void ol_future_destroy(ol_future_t *f);

/* Await completion; deadline_ns is absolute (monotonic). 
 * Returns: 1 completed, 0 timeout, -1 error.
 */
int ol_future_await(ol_future_t *f, int64_t deadline_ns);

/* Register a continuation to run on completion (exactly once).
 * If already complete, it is invoked immediately.
 * Returns 0 on success.
 */
int ol_future_then(ol_future_t *f, ol_future_cb cb, void *user_data);

/* Read-only value pointer if fulfilled and not taken; NULL otherwise. */
const void* ol_future_get_value_const(const ol_future_t *f);

/* Take ownership of value (move). Returns non-NULL only once; prevents destructor. */
void* ol_future_take_value(ol_future_t *f);

/* Error code if rejected; 0 otherwise. */
int ol_future_error(const ol_future_t *f);

/* Current state (non-blocking). */
ol_promise_state_t ol_future_state(const ol_future_t *f);

#ifdef __cplusplus
}
#endif

#endif /* OL_PROMISE_H */
