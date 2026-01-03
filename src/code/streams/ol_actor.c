/**
 * @file ol_actor.c
 * @brief Actor abstraction: mailbox, behavior dispatch, ask/reply envelopes, and lifecycle.
 *
 * Overview
 * --------
 * This module implements a lightweight actor model:
 * - Each actor owns a mailbox (ol_channel_t) for incoming messages.
 * - A behavior callback processes messages; behaviors may change via ol_actor_become.
 * - "Ask" semantics are supported via ol_actor_ask which enqueues an envelope containing
 *   a promise that the actor must resolve via reply helpers.
 * - Actors run on a parallel pool; actor_loop is submitted as a worker task.
 *
 * Ownership and memory model
 * --------------------------
 * - Mailbox items: the channel's destructor (msg_dtor) is authoritative for items enqueued.
 * - For plain messages, if the behavior consumes the message it must free it; otherwise
 *   the actor will call msg_dtor after dispatch.
 * - For ask envelopes, the actor behavior is expected to resolve and free the envelope using
 *   ol_actor_reply_ok/err/cancel. If the behavior ignores an ask envelope, the actor loop
 *   defensively cancels the promise and frees the envelope to avoid leaks.
 *
 * Thread-safety
 * -------------
 * - The actor struct contains a mutex (mu) protecting the running flag and behavior pointer.
 * - All public APIs that mutate actor state lock the mutex appropriately.
 *
 * Testing and tooling notes
 * -------------------------
 * - Unit tests should verify send/try_send/send_deadline semantics, ask/reply lifecycle,
 *   behavior swapping, and mailbox draining on stop/close.
 * - Static analyzers (clang-tidy, cppcheck) should be satisfied by explicit ownership comments.
 * - Sanitizers (ASan, TSan) will detect races if behavior implementations violate contracts.
 */

#include "ol_actor.h"

#include <stdlib.h>
#include <string.h>

/**
 * @struct ol_actor
 * @brief Internal actor representation.
 *
 * Fields:
 * - pool: parallel pool used to run the actor loop
 * - running: flag indicating actor should continue processing
 * - mailbox: channel for incoming messages
 * - msg_dtor: destructor for mailbox items (may be NULL)
 * - behavior: current behavior callback invoked for each message
 * - user_ctx: opaque user context passed to behaviors
 * - mu: mutex protecting running and behavior swap
 */
struct ol_actor {
    ol_parallel_pool_t *pool;
    bool running;
    ol_channel_t *mailbox;
    ol_chan_item_destructor msg_dtor;
    ol_actor_behavior behavior;
    void *user_ctx;
    ol_mutex_t mu;
};

/**
 * @brief Actor main loop executed on a worker thread.
 *
 * Loop semantics:
 * - Repeatedly checks running flag under mutex.
 * - Blocks on mailbox receive; handles return codes:
 *     * 1: item received -> dispatch to behavior
 *     * 0: mailbox closed and empty -> stop loop
 *     * -3: timeout -> continue (transient)
 *     * other negative: treat as transient error and continue
 * - Detects "ask" envelopes by peeking the reply pointer field; if reply != NULL treat as ask.
 * - Behavior convention:
 *     * If behavior consumes ownership of msg, it must set msg = NULL before returning.
 *     * If behavior returns br > 0, actor requests graceful stop.
 * - Memory management:
 *     * If behavior did not consume a non-ask message and msg_dtor is set, actor calls msg_dtor.
 *     * If behavior did not consume an ask envelope, actor cancels and destroys the promise and frees envelope.
 *
 * @param arg ol_actor_t* pointer
 */
static void actor_loop(void *arg) {
    ol_actor_t *a = (ol_actor_t*)arg;

    for (;;) {
        /* Check running state under lock */
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
            bool consumed = false;
            bool is_ask = false;

            /* Detect ask envelope via convention: envelope has reply pointer non-NULL */
            if (msg) {
                ol_ask_envelope_t *maybe = (ol_ask_envelope_t*)msg;
                if (maybe->reply != NULL) {
                    is_ask = true;
                }
            }

            if (beh) {
                br = beh(a, msg);
                /* Convention: if behavior takes ownership, it sets msg = NULL before returning. */
                if (msg == NULL) consumed = true;
            }

            if (br > 0) {
                /* Behavior requested graceful stop */
                ol_mutex_lock(&a->mu);
                a->running = false;
                ol_mutex_unlock(&a->mu);
            }

            /* If behavior did not consume a non-ask message, free via msg_dtor if present */
            if (!consumed && !is_ask && msg && a->msg_dtor) {
                a->msg_dtor(msg);
                msg = NULL;
            }

            /* Ask envelopes: if not consumed, cancel promise and free envelope to avoid leaks */
            if (!consumed && is_ask && msg) {
                ol_ask_envelope_t *env = (ol_ask_envelope_t*)msg;
                if (env->reply) {
                    (void)ol_promise_cancel(env->reply);
                    ol_promise_destroy(env->reply);
                }
                free(env);
                msg = NULL;
            }

        } else if (r == 0) {
            /* Mailbox closed and empty: stop actor */
            ol_mutex_lock(&a->mu);
            a->running = false;
            ol_mutex_unlock(&a->mu);
            break;
        } else if (r == -3) {
            /* Timeout: treat as transient and continue */
            continue;
        } else {
            /* Other error: continue to allow supervisor to decide */
            continue;
        }
    }
}

/* -------------------- Public API -------------------- */

/**
 * @brief Create a new actor instance.
 *
 * @param pool Parallel pool used to run actor loop (must be non-NULL)
 * @param capacity Mailbox capacity (0 = unbounded)
 * @param dtor Destructor for mailbox items (may be NULL)
 * @param initial Initial behavior callback (must be non-NULL)
 * @param user_ctx Opaque user context passed to behavior (may be NULL)
 * @return ol_actor_t* New actor handle or NULL on failure
 */
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

/**
 * @brief Start actor processing by submitting actor_loop to the pool.
 *
 * If actor is already running this is a no-op.
 *
 * @param a Actor handle
 * @return int 0 on success, -1 on invalid actor, or pool submit error code
 */
int ol_actor_start(ol_actor_t *a) {
    if (!a) return -1;
    ol_mutex_lock(&a->mu);
    if (a->running) { ol_mutex_unlock(&a->mu); return 0; }
    a->running = true;
    ol_mutex_unlock(&a->mu);

    return ol_parallel_submit(a->pool, actor_loop, a);
}

/**
 * @brief Gracefully stop actor: mark not running and close mailbox to allow loop to drain.
 *
 * @param a Actor handle
 * @return int 0 on success, -1 on invalid actor
 */
int ol_actor_stop(ol_actor_t *a) {
    if (!a) return -1;
    ol_mutex_lock(&a->mu);
    a->running = false;
    ol_mutex_unlock(&a->mu);
    ol_channel_close(a->mailbox);
    return 0;
}

/**
 * @brief Immediately close actor mailbox. Actor loop will stop when it observes closed mailbox.
 *
 * @param a Actor handle
 * @return int 0 on success, -1 on invalid actor
 */
int ol_actor_close(ol_actor_t *a) {
    if (!a) return -1;
    return ol_channel_close(a->mailbox);
}

/**
 * @brief Destroy actor and free resources. Performs a best-effort stop first.
 *
 * After this call the actor handle is invalid.
 *
 * @param a Actor handle
 */
void ol_actor_destroy(ol_actor_t *a) {
    if (!a) return;
    (void)ol_actor_stop(a);
    ol_mutex_destroy(&a->mu);
    ol_channel_destroy(a->mailbox);
    free(a);
}

/* -------------------- Messaging API -------------------- */

/**
 * @brief Send a message to actor mailbox (blocking).
 *
 * Ownership:
 * - Caller transfers ownership of msg to the mailbox; mailbox's destructor will be used
 *   if the message is not consumed by behavior.
 *
 * @param a Actor handle
 * @param msg Message pointer
 * @return int 0 on success, -1 on invalid actor, or channel error codes
 */
int ol_actor_send(ol_actor_t *a, void *msg) {
    if (!a) return -1;
    return ol_channel_send(a->mailbox, msg);
}

/**
 * @brief Send a message with deadline.
 *
 * @param a Actor handle
 * @param msg Message pointer
 * @param deadline_ns Absolute deadline in ns (0 = infinite)
 * @return int 0 on success, -1 on invalid actor, -3 timeout, -2 closed
 */
int ol_actor_send_deadline(ol_actor_t *a, void *msg, int64_t deadline_ns) {
    if (!a) return -1;
    return ol_channel_send_deadline(a->mailbox, msg, deadline_ns);
}

/**
 * @brief Try to send a message without blocking.
 *
 * @param a Actor handle
 * @param msg Message pointer
 * @return int 1 enqueued, 0 would-block, -2 closed, -1 invalid actor
 */
int ol_actor_try_send(ol_actor_t *a, void *msg) {
    if (!a) return -1;
    return ol_channel_try_send(a->mailbox, msg);
}

/* -------------------- Ask/Reply API -------------------- */

/**
 * @brief Enqueue an ask envelope and return a future to await the reply.
 *
 * Behavior:
 * - Allocates an ol_ask_envelope_t containing payload and a promise.
 * - Attempts a try_send; if mailbox is full, falls back to blocking send.
 * - On send failure (closed channel or error), rejects the promise and cleans up.
 *
 * Ownership:
 * - Envelope ownership is transferred to the mailbox/actor path.
 * - Payload ownership remains with the caller until actor resolves the envelope.
 *
 * @param a Actor handle
 * @param msg Payload pointer (may be NULL)
 * @return ol_future_t* Future to await reply, or NULL on failure
 */
ol_future_t* ol_actor_ask(ol_actor_t *a, void *msg) {
    if (!a) return NULL;

    /* Create promise/future pair */
    ol_promise_t *p = ol_promise_create(NULL);
    if (!p) return NULL;
    ol_future_t *f = ol_promise_get_future(p);
    if (!f) { ol_promise_destroy(p); return NULL; }

    /* Construct envelope: mailbox owns envelope; payload ownership remains with caller/behavior */
    ol_ask_envelope_t *env = (ol_ask_envelope_t*)calloc(1, sizeof(ol_ask_envelope_t));
    if (!env) { ol_future_destroy(f); ol_promise_destroy(p); return NULL; }
    env->payload = msg;
    env->reply   = p;

    /* Prefer try_send; fallback to blocking send if would-block */
    int r = ol_channel_try_send(a->mailbox, env);
    if (r == 1) {
        return f;
    }
    if (r == 0) {
        r = ol_channel_send(a->mailbox, env);
        if (r == 0) {
            return f;
        }
        /* blocking send failed: fall through to cleanup */
    } else if (r == -2) {
        /* closed: fail below */
    } else {
        /* error: fail below */
    }

    /* Failed to enqueue: reject promise and cleanup */
    (void)ol_promise_reject(p, /*error_code*/ -2);
    ol_promise_destroy(p);
    ol_future_destroy(f);
    free(env);
    return NULL;
}

/* -------------------- Behavior control and introspection -------------------- */

/**
 * @brief Swap actor behavior atomically.
 *
 * @param a Actor handle
 * @param next New behavior callback (must be non-NULL)
 * @return int 0 on success, -1 on invalid args
 */
int ol_actor_become(ol_actor_t *a, ol_actor_behavior next) {
    if (!a || !next) return -1;
    ol_mutex_lock(&a->mu);
    a->behavior = next;
    ol_mutex_unlock(&a->mu);
    return 0;
}

/**
 * @brief Retrieve user context pointer.
 *
 * @param a Actor handle
 * @return void* user context or NULL
 */
void* ol_actor_ctx(const ol_actor_t *a) {
    return a ? a->user_ctx : NULL;
}

/**
 * @brief Check whether actor is running.
 *
 * @param a Actor handle
 * @return bool true if running, false otherwise
 */
bool ol_actor_is_running(const ol_actor_t *a) {
    if (!a) return false;
    bool r;
    ol_mutex_lock((ol_mutex_t*)&a->mu);
    r = a->running;
    ol_mutex_unlock((ol_mutex_t*)&a->mu);
    return r;
}

/**
 * @brief Get current mailbox length (number of queued items).
 *
 * @param a Actor handle
 * @return size_t number of items or 0 on invalid actor
 */
size_t ol_actor_mailbox_len(const ol_actor_t *a) {
    if (!a) return 0;
    return ol_channel_len(a->mailbox);
}

/**
 * @brief Get mailbox capacity (0 = unbounded).
 *
 * @param a Actor handle
 * @return size_t capacity or 0 on invalid actor
 */
size_t ol_actor_mailbox_capacity(const ol_actor_t *a) {
    if (!a) return 0;
    return ol_channel_capacity(a->mailbox);
}

/* -------------------- Ask reply helpers for behaviors -------------------- */

/**
 * @brief Reply with success value to an ask envelope and free envelope.
 *
 * Behavior:
 * - Fulfills the promise, destroys the promise handle, and frees the envelope.
 *
 * @param env Ask envelope pointer
 * @param value Reply value pointer (may be NULL)
 * @param dtor Destructor for value (may be NULL)
 */
static inline void ol_actor_reply_ok(ol_ask_envelope_t *env, void *value, ol_value_destructor dtor) {
    if (!env || !env->reply) return;
    (void)ol_promise_fulfill(env->reply, value, dtor);
    ol_promise_destroy(env->reply);
    free(env);
}

/**
 * @brief Reply with error code to an ask envelope and free envelope.
 *
 * @param env Ask envelope pointer
 * @param error_code Integer error code
 */
static inline void ol_actor_reply_err(ol_ask_envelope_t *env, int error_code) {
    if (!env || !env->reply) return;
    (void)ol_promise_reject(env->reply, error_code);
    ol_promise_destroy(env->reply);
    free(env);
}

/**
 * @brief Cancel an ask envelope's promise and free envelope.
 *
 * @param env Ask envelope pointer
 */
static inline void ol_actor_reply_cancel(ol_ask_envelope_t *env) {
    if (!env || !env->reply) return;
    (void)ol_promise_cancel(env->reply);
    ol_promise_destroy(env->reply);
    free(env);
}