#include "ol_async.h"

#include <stdlib.h>
#include <string.h>

/* Internal wrapper passed to pool workers */
typedef struct {
    ol_promise_t *promise;
    ol_async_task_fn fn;
    void *arg;
    ol_value_destructor dtor;
} pool_task_ctx_t;

static void pool_task_wrapper(void *v) {
    pool_task_ctx_t *ctx = (pool_task_ctx_t*)v;
    if (!ctx) return;

    void *res = NULL;
    int rc = 0;
    /* Run user task; protect against crashes is caller responsibility */
    res = ctx->fn(ctx->arg);

    /* Fulfill promise. If user returned NULL it is a legitimate value. */
    if (ol_promise_fulfill(ctx->promise, res, ctx->dtor) != 0) {
        /* If promise resolution failed (race), free result via dtor if present */
        if (res && ctx->dtor) ctx->dtor(res);
    }

    /* Release the promise handle held by us */
    ol_promise_destroy(ctx->promise);
    free(ctx);
}

/* Public API */

ol_future_t* ol_async_run(ol_parallel_pool_t *pool, ol_async_task_fn fn, void *arg, ol_value_destructor dtor) {
    if (!pool || !fn) return NULL;

    /* Create promise/future */
    ol_promise_t *p = ol_promise_create(NULL);
    if (!p) return NULL;
    ol_future_t *f = ol_promise_get_future(p);
    if (!f) { ol_promise_destroy(p); return NULL; }

    pool_task_ctx_t *ctx = (pool_task_ctx_t*)calloc(1, sizeof(pool_task_ctx_t));
    if (!ctx) { ol_future_destroy(f); ol_promise_destroy(p); return NULL; }

    ctx->promise = p; /* pool task wrapper owns this promise and will destroy it */
    ctx->fn = fn;
    ctx->arg = arg;
    ctx->dtor = dtor;

    /* Submit to pool. The wrapper will run on a worker thread. */
    if (ol_parallel_submit(pool, pool_task_wrapper, ctx) != 0) {
        /* submission failed: cleanup */
        free(ctx);
        ol_future_destroy(f);
        ol_promise_destroy(p);
        return NULL;
    }

    return f;
}

/* Event-loop scheduling
 *
 * Implement by creating a small capture structure held by the posted event.
 * We use the loop's timer mechanism with zero delay as a portable way to schedule a callback
 * on the loop thread using ol_event_loop_register_timer with immediate deadline and one-shot.
 *
 * When cb runs inside the loop, it may either:
 * - return a non-NULL pointer which we treat as result and fulfill the promise, or
 * - use the provided promise to later fulfill/reject asynchronously and return NULL.
 *
 * Note: We allocate a tiny control block that the loop callback will free when done.
 */

typedef struct loop_task_ctx {
    ol_promise_t *promise;
    ol_async_loop_fn cb;
    void *arg;
    ol_value_destructor dtor;
    uint64_t timer_id; /* registered timer id for cleanup (if needed) */
} loop_task_ctx_t;

/* Loop callback invoked by event loop timers (one-shot). */
static void loop_task_trampoline(ol_event_loop_t *loop, ol_ev_type_t type, int fd, void *user_data) {
    (void)type; (void)fd;
    loop_task_ctx_t *ctx = (loop_task_ctx_t*)user_data;
    if (!ctx) return;

    /* Invoke user callback inside loop thread */
    void *ret = NULL;
    if (ctx->cb) {
        ret = ctx->cb(loop, ctx->arg, ctx->promise);
    }

    /* If cb returned a non-NULL value, fulfill the promise automatically */
    if (ret != NULL) {
        if (ol_promise_fulfill(ctx->promise, ret, ctx->dtor) != 0) {
            /* fulfillment failed: free via dtor */
            if (ctx->dtor) ctx->dtor(ret);
        }
    }

    /* Destroy the promise handle owned by this context */
    ol_promise_destroy(ctx->promise);

    /* Unregister timer if present (best-effort) */
    if (ctx->timer_id != 0) {
        (void)ol_event_loop_unregister(loop, ctx->timer_id);
        ctx->timer_id = 0;
    }

    /* Free control block */
    free(ctx);
}

ol_future_t* ol_async_run_on_loop(ol_event_loop_t *loop, ol_async_loop_fn cb, void *arg, ol_value_destructor dtor) {
    if (!loop || !cb) return NULL;

    ol_promise_t *p = ol_promise_create(loop);
    if (!p) return NULL;
    ol_future_t *f = ol_promise_get_future(p);
    if (!f) { ol_promise_destroy(p); return NULL; }

    loop_task_ctx_t *ctx = (loop_task_ctx_t*)calloc(1, sizeof(loop_task_ctx_t));
    if (!ctx) { ol_future_destroy(f); ol_promise_destroy(p); return NULL; }

    ctx->promise = p; /* owned by ctx; trampoline will destroy */
    ctx->cb = cb;
    ctx->arg = arg;
    ctx->dtor = dtor;
    ctx->timer_id = 0;

    /* Schedule via a one-shot immediate timer with deadline = now.
     * If the event loop supports direct task-posting, replace with that implementation.
     * Use ol_deadline_from_ns(0) meaning immediate (helper expects positive ns, so compute small offset).
     */
    ol_deadline_t dl = ol_deadline_from_ns(1); /* fire as soon as loop polls */
    uint64_t id = ol_event_loop_register_timer(loop, dl, 0, loop_task_trampoline, ctx);
    if (id == 0) {
        /* scheduling failed */
        free(ctx);
        ol_future_destroy(f);
        ol_promise_destroy(p);
        return NULL;
    }
    ctx->timer_id = id;
    return f;
}
