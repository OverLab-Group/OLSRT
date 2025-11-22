#ifndef OL_CHANNELS_H
#define OL_CHANNELS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_lock_mutex.h"
#include "ol_deadlines.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ol_channel ol_channel_t;

/* Items are opaque pointers; channel may own items if a destructor is set. */
typedef void (*ol_chan_item_destructor)(void *item);

/* Create a channel.
 * capacity:
 *   - >0 for bounded queue
 *   - 0 for unbounded queue
 * dtor:
 *   - optional destructor for items owned by the channel on recv/drop/close.
 */
ol_channel_t* ol_channel_create(size_t capacity, ol_chan_item_destructor dtor);

/* Destroy channel object and free resources.
 * Auto-closes the channel. If items remain enqueued and a destructor is set, they are freed.
 */
void ol_channel_destroy(ol_channel_t *ch);

/* Close channel: disallow new sends; wake all waiters.
 * Receivers can still drain existing items.
 */
int ol_channel_close(ol_channel_t *ch);

/* Send an item (blocking until space or deadline).
 * Returns:
 *   0  on success,
 *  -1  on error (invalid args),
 *  -2  if channel is closed (item is dropped; destructor called if set),
 *   0  on timeout (if deadline != 0 and expired before enqueue).
 * Note: to distinguish timeout, use ol_channel_send_deadline().
 */
int ol_channel_send(ol_channel_t *ch, void *item);

/* Same as send, but with deadline (absolute ns). Returns 0 success, -2 closed, -3 timeout, -1 error. */
int ol_channel_send_deadline(ol_channel_t *ch, void *item, int64_t deadline_ns);

/* Try to send without blocking. Returns 1 success, 0 would-block (full), -2 closed, -1 error. */
int ol_channel_try_send(ol_channel_t *ch, void *item);

/* Receive an item (blocking until available or deadline).
 * On success: returns 1 and writes *out = item.
 * Returns 0 if channel closed and empty, -3 timeout, -1 error.
 */
int ol_channel_recv(ol_channel_t *ch, void **out);

/* Same as recv, but with deadline (absolute ns). Returns 1 success; 0 closed+empty; -3 timeout; -1 error. */
int ol_channel_recv_deadline(ol_channel_t *ch, void **out, int64_t deadline_ns);

/* Try to receive without blocking. Returns 1 success; 0 would-block (empty); -1 error. */
int ol_channel_try_recv(ol_channel_t *ch, void **out);

/* Introspection */
bool   ol_channel_is_closed(const ol_channel_t *ch);
size_t ol_channel_len(const ol_channel_t *ch);
size_t ol_channel_capacity(const ol_channel_t *ch);

#ifdef __cplusplus
}
#endif

#endif /* OL_CHANNELS_H */
