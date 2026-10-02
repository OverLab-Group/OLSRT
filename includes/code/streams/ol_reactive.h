#ifndef OL_REACTIVE_H
#define OL_REACTIVE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_event_loop.h"
#include "ol_deadlines.h"
#include "ol_lock_mutex.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ol_item_destructor)(void *item);

typedef void (*ol_rx_on_next)(void *item, void *user_data);
typedef void (*ol_rx_on_error)(int error_code, void *user_data);
typedef void (*ol_rx_on_complete)(void *user_data);

/* Operators' function types must be declared before structs that use them */
typedef void* (*ol_rx_map_fn)(const void *item, void *user_data);
typedef bool  (*ol_rx_filter_fn)(const void *item, void *user_data);

typedef struct fd_ctx {
    int fd;
    uint32_t mask;
    uint64_t reg_id;
} fd_ctx_t;

typedef enum {
    RX_PENDING = 0,
    RX_ERROR,
    RX_COMPLETE
} rx_state_t;

typedef struct rx_item_node {
    void *item;
    struct rx_item_node *next;
} rx_item_node_t;

typedef struct op_ctx_map {
    ol_rx_map_fn fn;
    void *user_data;
    ol_item_destructor out_dtor;
} op_ctx_map_t;

typedef struct op_ctx_filter {
    ol_rx_filter_fn pred;
    void *user_data;
} op_ctx_filter_t;

typedef struct op_ctx_take {
    size_t remaining;
} op_ctx_take_t;

typedef struct op_ctx_debounce {
    int64_t interval_ns;
    uint64_t timer_id;
    bool     have_pending;
    void    *last_item;
} op_ctx_debounce_t;

typedef struct ol_observable    ol_observable_t;

typedef struct ol_subscription {
    ol_observable_t *parent;
    ol_rx_on_next on_next;
    ol_rx_on_error on_error;
    ol_rx_on_complete on_complete;
    void *user_data;
    size_t demand;
    bool unsubscribed;
    struct ol_subscription *next;
} ol_rx_subscription_t;

typedef struct ol_subject       ol_subject_t;

/* Create a subject (hot observable) with optional item destructor for ownership. */
/**
 * @brief Create a subject (a hot observable driven by the caller).
 *
 * @param loop Event loop that drives timers and I/O for operators
 *             derived from this subject.
 * @param dtor Destructor applied to items the subject owns.
 * @return Subject handle, or NULL on failure.
 * @see ol_subject_destroy, ol_subject_on_next
 */

ol_subject_t* ol_subject_create(ol_event_loop_t *loop, ol_item_destructor dtor);

/* Destroy subject (completes, frees resources). */
/**
 * @brief Destroy a subject and release its resources.
 *
 * @param s Subject handle; NULL is a no-op.
 */

void ol_subject_destroy(ol_subject_t *s);

/* Subject API: push signals into the subject. */
/**
 * @brief Push one item into the subject.
 *
 * @param s    Subject handle.
 * @param item Item pointer; ownership follows the subject's
 *             destructor.
 * @return 0 on success, -1 on error.
 */

int ol_subject_on_next(ol_subject_t *s, void *item);
/**
 * @brief Signal an error to subscribers.
 *
 * @param s          Subject handle.
 * @param error_code Error code forwarded to observers.
 * @return 0 on success, -1 on error.
 */

int ol_subject_on_error(ol_subject_t *s, int error_code);
/**
 * @brief Signal normal completion to subscribers.
 *
 * @param s Subject handle.
 * @return 0 on success, -1 on error.
 */

int ol_subject_on_complete(ol_subject_t *s);

/* Convert subject to observable (returned pointer is the same underlying object). */
/**
 * @brief Return the observable view of a subject.
 *
 * @param s Subject handle.
 * @return Observable pointer that shares the subject's lifetime.
 */

ol_observable_t* ol_subject_as_observable(ol_subject_t *s);

/* Create an empty cold observable (user will emit via internal API or operators will drive source).
 * Usually you use operators (timer/from_fd) to create sources.
 */
/**
 * @brief Create an empty cold observable.
 *
 * @param loop Event loop that drives derived operators.
 * @param dtor Item destructor.
 * @return Observable handle, or NULL on failure.
 * @see ol_observable_destroy, ol_observable_subscribe
 */

ol_observable_t* ol_observable_create(ol_event_loop_t *loop, ol_item_destructor dtor);

/* Destroy observable (completes, frees). */
/**
 * @brief Destroy an observable.
 *
 * @param o Observable handle; NULL is a no-op. Active subscriptions
 *          observe a completion notification.
 */

void ol_observable_destroy(ol_observable_t *o);

/* Subscribe to an observable. Returns a subscription handle or NULL.
 * demand: initial requested item count (0 => caller will request later).
 */
/**
 * @brief Subscribe to an observable.
 *
 * @param o           Observable handle.
 * @param on_next     Callback invoked for each item.
 * @param on_error    Callback invoked on error (may be NULL).
 * @param on_complete Callback invoked on completion (may be NULL).
 * @param demand      Initial demand.
 * @param user_data   Opaque pointer forwarded to all callbacks.
 * @return Subscription handle, or NULL on failure.
 * @see ol_rx_request, ol_rx_unsubscribe
 */

ol_rx_subscription_t* ol_observable_subscribe(
    ol_observable_t *o,
    ol_rx_on_next on_next,
    ol_rx_on_error on_error,
    ol_rx_on_complete on_complete,
    size_t demand,
    void *user_data
);

/* Request more items (backpressure). */
/**
 * @brief Request more items from an observable.
 *
 * @param sub Subscription handle.
 * @param n   Number of additional items to request.
 * @return 0 on success, -1 on error.
 */

int ol_rx_request(ol_rx_subscription_t *sub, size_t n);

/* Unsubscribe and destroy subscription. Idempotent. */
/**
 * @brief Stop receiving items on a subscription.
 *
 * @param sub Subscription handle.
 * @return 0 on success, -1 on error. Idempotent.
 */

int  ol_rx_unsubscribe(ol_rx_subscription_t *sub);
/**
 * @brief Destroy a subscription object.
 *
 * @param sub Subscription handle. Call after unsubscribe or
 *            completion to release the handle itself.
 */

void ol_rx_subscription_destroy(ol_rx_subscription_t *sub);

/* Operators: return new observable bound to the same loop. Caller must destroy. */

/* Map: transform item => new_item (ownership controlled by out_dtor). */
/**
 * @brief Transform each item with a user function.
 *
 * @param src         Source observable.
 * @param fn          Map function returning the transformed item.
 * @param user_data   Opaque pointer forwarded to @p fn.
 * @param out_dtor    Destructor for transformed items.
 * @return New observable handle, or NULL on failure.
 */

ol_observable_t* ol_rx_map(ol_observable_t *src, ol_rx_map_fn fn, void *user_data, ol_item_destructor out_dtor);

/* Filter: pass items where pred(item) is true. */
/**
 * @brief Drop items for which the predicate returns false.
 *
 * @param src       Source observable.
 * @param pred      Predicate function.
 * @param user_data Opaque pointer forwarded to @p pred.
 * @return New observable handle, or NULL on failure.
 */

ol_observable_t* ol_rx_filter(ol_observable_t *src, ol_rx_filter_fn pred, void *user_data);

/* Take: take first N items then complete. */
/**
 * @brief Forward only the first @p n items, then complete.
 *
 * @param src Source observable.
 * @param n   Number of items to forward.
 * @return New observable handle, or NULL on failure.
 */

ol_observable_t* ol_rx_take(ol_observable_t *src, size_t n);

/* Merge: interleave items from two observables. */
/**
 * @brief Interleave items from two source observables.
 *
 * @param a         First source observable.
 * @param b         Second source observable.
 * @param dtor_hint Destructor for items emitted by the merged stream.
 * @return New observable handle, or NULL on failure.
 */

ol_observable_t* ol_rx_merge(ol_observable_t *a, ol_observable_t *b, ol_item_destructor dtor_hint);

/* Debounce: emit last item only if interval passes without a new one. */
/**
 * @brief Emit an item only after a quiet interval.
 *
 * @param src         Source observable.
 * @param interval_ns Quiet interval in nanoseconds.
 * @return New observable handle, or NULL on failure.
 */

ol_observable_t* ol_rx_debounce(ol_observable_t *src, int64_t interval_ns);

/* Timer: emit NULL ticks periodically; count=1 => one-shot. */
/**
 * @brief Create an observable that ticks on a timer.
 *
 * @param loop      Event loop that drives the timer.
 * @param period_ns Period between ticks in nanoseconds.
 * @param count     Number of ticks (1 for a one-shot).
 * @return New observable handle, or NULL on failure.
 */

ol_observable_t* ol_rx_timer(ol_event_loop_t *loop, int64_t period_ns, size_t count);

/* From fd: emit NULL when fd is ready for mask (OL_POLL_IN/OUT). */
/**
 * @brief Create an observable that emits on I/O readiness.
 *
 * @param loop Event loop that drives the poller.
 * @param fd   File descriptor to watch; not owned by the observable.
 * @param mask Poll mask (OL_POLL_IN / OL_POLL_OUT).
 * @return New observable handle, or NULL on failure.
 */

ol_observable_t* ol_rx_from_fd(ol_event_loop_t *loop, int fd, uint32_t mask);

/* Introspection */
/**
 * @brief Check whether an observable has completed or errored.
 *
 * @param o Observable handle.
 * @return true if the observable is in a terminal state.
 */

bool   ol_rx_completed(const ol_observable_t *o);
/**
 * @brief Return the number of active subscribers.
 *
 * @param o Observable handle.
 * @return Subscriber count, or 0 if @p o is NULL.
 */

size_t ol_rx_subscriber_count(const ol_observable_t *o);

#ifdef __cplusplus
}
#endif

#endif /* OL_REACTIVE_H */
