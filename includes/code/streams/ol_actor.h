#ifndef OL_ACTOR_H
#define OL_ACTOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_lock_mutex.h"
#include "ol_channel.h"
#include "ol_deadlines.h"
#include "ol_parallel.h"
#include "ol_promise.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ol_actor ol_actor_t;

/* Message envelope for ask pattern.
 * behavior receives either raw user messages (void*) or ask envelopes when using ask API.
 */
typedef struct {
    void  *payload;       /* user message */
    ol_promise_t *reply;  /* reply promise (owned by the envelope; actor fulfills/rejects) */
} ol_ask_envelope_t;

/* Actor behavior signature.
 * - self: current actor
 * - msg:  received message (either user payload or ol_ask_envelope_t* if it is an ask envelope)
 * Return:
 *   0 continue,
 *  >0 request graceful stop,
 *  <0 error (will be logged or handled by supervisor if any)
 */
typedef int (*ol_actor_behavior)(ol_actor_t *self, void *msg);

/* Lifecycle */

/* Create an actor:
 * - pool: thread pool to run this actor (required)
 * - capacity: mailbox capacity (0 => unbounded)
 * - dtor: optional destructor for messages owned by actor (applied on drop at stop/close)
 * - initial: initial behavior
 * - user_ctx: arbitrary context stored in actor (accessible via ol_actor_ctx)
 */
ol_actor_t* ol_actor_create(ol_parallel_pool_t *pool,
                            size_t capacity,
                            ol_chan_item_destructor dtor,
                            ol_actor_behavior initial,
                            void *user_ctx);

/* Start the actor’s loop on the pool (idempotent). */
int ol_actor_start(ol_actor_t *a);

/* Request graceful stop (drain mailbox, then exit). */
int ol_actor_stop(ol_actor_t *a);

/* Close mailbox immediately; pending messages are dropped. Actor will stop next tick. */
int ol_actor_close(ol_actor_t *a);

/* Destroy actor and free resources (implies stop). */
void ol_actor_destroy(ol_actor_t *a);

/* Messaging */

/* send: enqueue a message (blocking until space or deadline via ol_actor_send_deadline). */
int ol_actor_send(ol_actor_t *a, void *msg);

/* send with deadline: absolute ns; returns 0 success, -3 timeout, -2 closed, -1 error. */
int ol_actor_send_deadline(ol_actor_t *a, void *msg, int64_t deadline_ns);

/* try_send: non-blocking; returns 1 success, 0 full, -2 closed, -1 error. */
int ol_actor_try_send(ol_actor_t *a, void *msg);

/* ask: request/reply. Creates a promise, enqueues envelope; returns future for awaiting reply. */
ol_future_t* ol_actor_ask(ol_actor_t *a, void *msg);

/* Behavior control */

/* become: swap current behavior function; takes effect for subsequent messages. */
int ol_actor_become(ol_actor_t *a, ol_actor_behavior next);

/* Context and introspection */

/* Retrieve user context pointer. */
void* ol_actor_ctx(const ol_actor_t *a);

/* Is actor running? */
bool  ol_actor_is_running(const ol_actor_t *a);

/* Mailbox stats */
size_t ol_actor_mailbox_len(const ol_actor_t *a);
size_t ol_actor_mailbox_capacity(const ol_actor_t *a);

#ifdef __cplusplus
}
#endif

#endif /* OL_ACTOR_H */
