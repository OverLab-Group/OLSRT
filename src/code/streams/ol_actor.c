#include "ol_actor.h"

#include <stdlib.h>
#include <string.h>

/* Actor structure */
struct ol_actor {
    /* Execution */
    ol_parallel_pool_t *pool;
    bool running;

    /* Mailbox */
    ol_channel_t *mailbox;
    ol_chan_item_destructor msg_dtor;

    /* Behavior */
    ol_actor_behavior behavior;

    /* User context */
    void *user_ctx;

    /* Sync for behavior swap and running flag */
    ol_mutex_t mu;
};

/* Internal: actor loop on a worker thread */
static void actor_loop(void *arg) {
    ol_actor_t *a = (ol_actor_t*)arg;

    for (;;) {
        /* Check running state */
        ol_mutex_lock(&a->mu);
        bool run = a->running;
        ol_mutex_unlock(&a->mu);
        if (!run) break;

        /* Receive next message (blocking) */
        void *msg = NULL;
        int r = ol_channel_recv(a->mailbox, &msg);

        if (r == 1) {
            /* Dispatch */
            ol_actor_behavior beh;

            ol_mutex_lock(&a->mu);
            beh = a->behavior;
            ol_mutex_unlock(&a->mu);

            int br = 0;
            if (beh) {
                br = beh(a, msg);
            }

            /* If message is ask envelope, we do not own payload; envelope creator handles reply lifecycle.
             * If actor owns messages (msg_dtor) and message was not an ask envelope and not forwarded,
             * the actor behavior should free or forward. As a safety net, if msg_dtor is set, we free here
             * when behavior returns without consuming, assuming ownership. This convention requires
             * behaviors to set msg to NULL when they take over ownership.
             */

            /* Heuristic: if message appears to be ol_ask_envelope_t, do not free here. */
            bool is_ask = false;
            if (msg) {
                /* We can’t reliably type-check; we rely on protocol during ask enqueue. */
                /* Leave item management to behavior for general messages. */
            }

            if (br > 0) {
                /* graceful stop requested */
                ol_mutex_lock(&a->mu);
                a->running = false;
                ol_mutex_unlock(&a->mu);
            }

            /* If actor owns messages and behavior did not free, free now. */
            if (!is_ask && msg && a->msg_dtor) {
                a->msg_dtor(msg);
                msg = NULL;
            }

        } else if (r == 0) {
            /* Mailbox closed and empty: stop */
            ol_mutex_lock(&a->mu);
            a->running = false;
            ol_mutex_unlock(&a->mu);
            break;
        } else {
            /* Error or timeout: treat as transient, continue */
        }
    }
}

/* Public API */

ol_actor_t* ol_actor_create(ol_parallel_pool_t *pool,
                            size_t capacity,
                            ol_chan_item_destructor dtor,
                            ol_actor_behavior initial,
                            void *user_ctx)
{
    if (!pool || !initial) return NULL;

    ol_actor_t *a = (ol_actor_t*)calloc(1, sizeof(ol_actor_t));
    if (!a) return NULL;

    a->pool = pool;
    a->running = false;
    a->mailbox = ol_channel_create(capacity, dtor);
    if (!a->mailbox) { free(a); return NULL; }
    a->msg_dtor = dtor;
    a->behavior = initial;
    a->user_ctx = user_ctx;

    if (ol_mutex_init(&a->mu) != 0) {
        ol_channel_destroy(a->mailbox);
        free(a);
        return NULL;
    }

    return a;
}

int ol_actor_start(ol_actor_t *a) {
    if (!a) return -1;
    ol_mutex_lock(&a->mu);
    if (a->running) { ol_mutex_unlock(&a->mu); return 0; }
    a->running = true;
    ol_mutex_unlock(&a->mu);

    return ol_parallel_submit(a->pool, actor_loop, a);
}

int ol_actor_stop(ol_actor_t *a) {
    if (!a) return -1;
    /* Graceful: mark not running, but keep mailbox to drain by loop until empty */
    ol_mutex_lock(&a->mu);
    a->running = false;
    ol_mutex_unlock(&a->mu);
    /* Nudge receivers: close mailbox so recv returns 0 when empty */
    ol_channel_close(a->mailbox);
    return 0;
}

int ol_actor_close(ol_actor_t *a) {
    if (!a) return -1;
    /* Immediate close: closes mailbox; actor loop will stop next tick */
    return ol_channel_close(a->mailbox);
}

void ol_actor_destroy(ol_actor_t *a) {
    if (!a) return;
    (void)ol_actor_stop(a);
    ol_mutex_destroy(&a->mu);
    ol_channel_destroy(a->mailbox);
    free(a);
}

int ol_actor_send(ol_actor_t *a, void *msg) {
    if (!a) return -1;
    return ol_channel_send(a->mailbox, msg);
}

int ol_actor_send_deadline(ol_actor_t *a, void *msg, int64_t deadline_ns) {
    if (!a) return -1;
    return ol_channel_send_deadline(a->mailbox, msg, deadline_ns);
}

int ol_actor_try_send(ol_actor_t *a, void *msg) {
    if (!a) return -1;
    return ol_channel_try_send(a->mailbox, msg);
}

/* Ask: enqueue envelope and return future to await reply. */
ol_future_t* ol_actor_ask(ol_actor_t *a, void *msg) {
    if (!a) return NULL;

    /* Create promise/future pair */
    ol_promise_t *p = ol_promise_create(NULL);
    if (!p) return NULL;
    ol_future_t *f = ol_promise_get_future(p);
    if (!f) { ol_promise_destroy(p); return NULL; }

    /* Construct envelope (owned by mailbox; actor behavior must fulfill/reject/cancel and free payload if needed) */
    ol_ask_envelope_t *env = (ol_ask_envelope_t*)calloc(1, sizeof(ol_ask_envelope_t));
    if (!env) { ol_future_destroy(f); ol_promise_destroy(p); return NULL; }
    env->payload = msg;
    env->reply   = p;

    /* For envelope messages, channel destructor should free envelope on drop; payload ownership remains user-managed.
     * We temporarily override mailbox destructor for this send by wrapping; but channel API doesn’t support per-item dtor.
     * Therefore, we rely on actor behavior to always consume envelope and free it. If mailbox drops due to close,
     * we free envelope here by detecting return codes.
     */
    int r = ol_channel_try_send(a->mailbox, env);
    if (r == 1) {
        /* Enqueued; actor behavior must free env eventually */
        return f;
    }
    /* Fallback to blocking send */
    if (r == 0) {
        r = ol_channel_send(a->mailbox, env);
        if (r == 0) return f;
    }
    /* Failed: free resources */
    free(env);
    ol_future_destroy(f);
    ol_promise_destroy(p);
    return NULL;
}

/* Behavior control */

int ol_actor_become(ol_actor_t *a, ol_actor_behavior next) {
    if (!a || !next) return -1;
    ol_mutex_lock(&a->mu);
    a->behavior = next;
    ol_mutex_unlock(&a->mu);
    return 0;
}

/* Context and stats */

void* ol_actor_ctx(const ol_actor_t *a) {
    return a ? a->user_ctx : NULL;
}

bool ol_actor_is_running(const ol_actor_t *a) {
    if (!a) return false;
    bool r;
    ol_mutex_lock((ol_mutex_t*)&a->mu);
    r = a->running;
    ol_mutex_unlock((ol_mutex_t*)&a->mu);
    return r;
}

size_t ol_actor_mailbox_len(const ol_actor_t *a) {
    if (!a) return 0;
    return ol_channel_len(a->mailbox);
}

size_t ol_actor_mailbox_capacity(const ol_actor_t *a) {
    if (!a) return 0;
    return ol_channel_capacity(a->mailbox);
}

/* Helper: typical ask reply API for behaviors */

static inline void ol_actor_reply_ok(ol_ask_envelope_t *env, void *value, ol_value_destructor dtor) {
    if (!env || !env->reply) return;
    (void)ol_promise_fulfill(env->reply, value, dtor);
    ol_promise_destroy(env->reply);
    free(env);
}

static inline void ol_actor_reply_err(ol_ask_envelope_t *env, int error_code) {
    if (!env || !env->reply) return;
    (void)ol_promise_reject(env->reply, error_code);
    ol_promise_destroy(env->reply);
    free(env);
}

static inline void ol_actor_reply_cancel(ol_ask_envelope_t *env) {
    if (!env || !env->reply) return;
    (void)ol_promise_cancel(env->reply);
    ol_promise_destroy(env->reply);
    free(env);
}

/* Example behavior pattern (for reference, not part of public header):
 * int my_behavior(ol_actor_t *self, void *msg) {
 *     ol_ask_envelope_t *env = (ol_ask_envelope_t*)msg; // if ask
 *     // Or treat msg as your own payload otherwise.
 *     // Use ol_actor_reply_ok(env, result_ptr, destructor) to reply.
 *     return 0;
 * }
 */

