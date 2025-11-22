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
ol_subject_t* ol_subject_create(ol_event_loop_t *loop, ol_item_destructor dtor);

/* Destroy subject (completes, frees resources). */
void ol_subject_destroy(ol_subject_t *s);

/* Subject API: push signals into the subject. */
int ol_subject_on_next(ol_subject_t *s, void *item);
int ol_subject_on_error(ol_subject_t *s, int error_code);
int ol_subject_on_complete(ol_subject_t *s);

/* Convert subject to observable (returned pointer is the same underlying object). */
ol_observable_t* ol_subject_as_observable(ol_subject_t *s);

/* Create an empty cold observable (user will emit via internal API or operators will drive source).
 * Usually you use operators (timer/from_fd) to create sources.
 */
ol_observable_t* ol_observable_create(ol_event_loop_t *loop, ol_item_destructor dtor);

/* Destroy observable (completes, frees). */
void ol_observable_destroy(ol_observable_t *o);

/* Subscribe to an observable. Returns a subscription handle or NULL.
 * demand: initial requested item count (0 => caller will request later).
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
int ol_rx_request(ol_rx_subscription_t *sub, size_t n);

/* Unsubscribe and destroy subscription. Idempotent. */
int  ol_rx_unsubscribe(ol_rx_subscription_t *sub);
void ol_rx_subscription_destroy(ol_rx_subscription_t *sub);

/* Operators: return new observable bound to the same loop. Caller must destroy. */

/* Map: transform item => new_item (ownership controlled by out_dtor). */
ol_observable_t* ol_rx_map(ol_observable_t *src, ol_rx_map_fn fn, void *user_data, ol_item_destructor out_dtor);

/* Filter: pass items where pred(item) is true. */
ol_observable_t* ol_rx_filter(ol_observable_t *src, ol_rx_filter_fn pred, void *user_data);

/* Take: take first N items then complete. */
ol_observable_t* ol_rx_take(ol_observable_t *src, size_t n);

/* Merge: interleave items from two observables. */
ol_observable_t* ol_rx_merge(ol_observable_t *a, ol_observable_t *b, ol_item_destructor dtor_hint);

/* Debounce: emit last item only if interval passes without a new one. */
ol_observable_t* ol_rx_debounce(ol_observable_t *src, int64_t interval_ns);

/* Timer: emit NULL ticks periodically; count=1 => one-shot. */
ol_observable_t* ol_rx_timer(ol_event_loop_t *loop, int64_t period_ns, size_t count);

/* From fd: emit NULL when fd is ready for mask (OL_POLL_IN/OUT). */
ol_observable_t* ol_rx_from_fd(ol_event_loop_t *loop, int fd, uint32_t mask);

/* Introspection */
bool   ol_rx_completed(const ol_observable_t *o);
size_t ol_rx_subscriber_count(const ol_observable_t *o);

#ifdef __cplusplus
}
#endif

#endif /* OL_REACTIVE_H */
