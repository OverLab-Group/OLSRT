#ifndef OL_STREAMS_H
#define OL_STREAMS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_event_loop.h"
#include "ol_deadlines.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ol_stream        ol_stream_t;
typedef struct ol_subscription  ol_subscription_t;

/* Item destructor for values owned by the stream (operators may copy/transform). */
typedef void (*ol_item_destructor)(void *item);

/* Observer callbacks; all are optional but on_next is strongly recommended. */
typedef void (*ol_on_next_fn)(void *item, void *user_data);
typedef void (*ol_on_error_fn)(int error_code, void *user_data);
typedef void (*ol_on_complete_fn)(void *user_data);

/* Create a cold stream with optional item destructor.
 * Items are emitted by calling ol_stream_emit_* or via operator sources.
 */
ol_stream_t* ol_stream_create(ol_event_loop_t *loop, ol_item_destructor dtor);

/* Destroy a stream and release internal resources.
 * Safe even if subscriptions remain; they will see completion with error = canceled.
 */
void ol_stream_destroy(ol_stream_t *s);

/* Subscribe to a stream. Returns a subscription handle or NULL on failure.
 * demand: initial requested item count for backpressure (0 means caller will request later).
 * user_data: passed to callbacks.
 */
ol_subscription_t* ol_stream_subscribe(
    ol_stream_t *s,
    ol_on_next_fn on_next,
    ol_on_error_fn on_error,
    ol_on_complete_fn on_complete,
    size_t demand,
    void *user_data
);

/* Request more items on a subscription (cooperative backpressure).
 * Returns 0 on success.
 */
int ol_subscription_request(ol_subscription_t *sub, size_t n);

/* Unsubscribe: stop receiving further items. Idempotent. */
int ol_subscription_unsubscribe(ol_subscription_t *sub);

/* Destroy subscription object (after unsubscribe or completion). */
void ol_subscription_destroy(ol_subscription_t *sub);

/* Emit API (for source streams):
 * - push items (owned by the stream unless operators override ownership)
 * - signal error or completion
 * Returns 0 on success.
 */
int ol_stream_emit_next(ol_stream_t *s, void *item);
int ol_stream_emit_error(ol_stream_t *s, int error_code);
int ol_stream_emit_complete(ol_stream_t *s);

/* Operators: all return a new stream that depends on the input stream.
 * The returned stream must be destroyed by the caller.
 */

/* Map: transform items; map(item) => new_item
 * map_fn returns the transformed item pointer. Optional out_dtor owns transformed items.
 */
typedef void* (*ol_map_fn)(const void *item, void *user_data);
ol_stream_t* ol_stream_map(ol_stream_t *src, ol_map_fn fn, void *user_data, ol_item_destructor out_dtor);

/* Filter: pass only items where pred(item) == true */
typedef bool (*ol_filter_fn)(const void *item, void *user_data);
ol_stream_t* ol_stream_filter(ol_stream_t *src, ol_filter_fn pred, void *user_data);

/* Take: take first N items then complete */
ol_stream_t* ol_stream_take(ol_stream_t *src, size_t n);

/* Merge: interleave items from a and b */
ol_stream_t* ol_stream_merge(ol_stream_t *a, ol_stream_t *b, ol_item_destructor dtor_hint);

/* Debounce: emit last item only after interval without new items */
ol_stream_t* ol_stream_debounce(ol_stream_t *src, int64_t interval_ns);

/* Timer: create a stream that ticks every period_ns (periodic) or once if count==1.
 * Emits NULL items (or user can treat tick as sentinel).
 */
ol_stream_t* ol_stream_timer(ol_event_loop_t *loop, int64_t period_ns, size_t count);

/* From fd: emit a sentinel (NULL) whenever fd is readable/writable according to mask.
 * The stream does not own the fd; caller closes it.
 */
ol_stream_t* ol_stream_from_fd(ol_event_loop_t *loop, int fd, uint32_t mask);

/* Introspection */
bool   ol_stream_is_completed(const ol_stream_t *s);
size_t ol_stream_subscriber_count(const ol_stream_t *s);

#ifdef __cplusplus
}
#endif

#endif /* OL_STREAMS_H */
