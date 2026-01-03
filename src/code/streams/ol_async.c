/**
 * @file ol_async.c
 * @brief Asynchronous task execution helpers: run tasks on a parallel pool or schedule
 *        callbacks on an event loop thread and return futures.
 *
 * Overview
 * --------
 * This module provides two complementary execution primitives:
 * - ol_async_run: execute a blocking or CPU-bound task on a thread pool and obtain a future.
 * - ol_async_run_on_loop: schedule a callback to run on an event-loop thread and obtain a future.
 *
 * Design goals and guarantees
 * ---------------------------
 * - Memory ownership: callers supply an optional destructor for result values. If the promise
 *   resolution fails due to races or shutdown, the module will call the destructor to avoid leaks.
 * - Threading: pool tasks run on worker threads; loop tasks run on the event-loop thread.
 * - Promise lifecycle: the wrapper owns the promise it creates and destroys it after resolution.
 * - Failure modes: functions return NULL on allocation or scheduling failure; callers must check.
 *
 * Testing and tooling notes
 * -------------------------
 * - Unit tests (CUnit, Catch2, Unity) should exercise success, failure, and destructor paths.
 * - Fuzzers (AFL++, libFuzzer) can call ol_async_run with synthetic tasks to validate memory safety.
 * - Sanitizers (ASan, UBSan, TSan) will benefit from explicit ownership comments and destructor usage.
 * - Benchmarks (Google Benchmark, perf) should measure overhead of scheduling vs direct call.
 *
 * API contracts
 * -------------
 * - ol_async_run:
 *     * pool and fn must be non-NULL.
 *     * Returned future must be destroyed by caller (ol_future_destroy).
 *     * Result destructor (dtor) is invoked by promise core when appropriate.
 * - ol_async_run_on_loop:
 *     * loop and cb must be non-NULL.
 *     * Callback may either return a non-NULL result (immediate fulfillment) or use the
 *       provided promise to fulfill/reject later and return NULL.
 *
 * Thread-safety
 * -------------
 * - This module itself is thread-safe: pool submissions and event-loop registration are delegated
 *   to the respective subsystems which must be thread-safe.
 */

#include "ol_async.h"

#include <stdlib.h>
#include <string.h>

/**
 * @struct pool_task_ctx_t
 * @brief Internal context passed to pool workers.
 *
 * Fields:
 * - promise: promise to fulfill when the task completes (owned by this context until destroyed)
 * - fn: user-provided task function to execute on worker thread
 * - arg: argument forwarded to fn
 * - dtor: optional destructor for the returned value
 *
 * Ownership:
 * - The pool worker wrapper owns this structure and is responsible for freeing it.
 */
typedef struct {
    ol_promise_t *promise;
    ol_async_task_fn fn;
    void *arg;
    ol_value_destructor dtor;
} pool_task_ctx_t;

/**
 * @brief Pool worker wrapper that executes the user task and resolves the promise.
 *
 * Behavior:
 * - Calls ctx->fn(ctx->arg) on the worker thread.
 * - If the user returns a value, attempts to fulfill the promise with that value and the
 *   provided destructor.
 * - If promise fulfillment fails (e.g., race or already-resolved), the wrapper will call
 *   the destructor to avoid leaking the returned value.
 * - Always destroys the promise handle it owns and frees the context.
 *
 * Safety:
 * - The wrapper does not catch crashes inside user code; callers should ensure tasks are robust.
 *
 * @param v Pointer to pool_task_ctx_t (must not be NULL)
 */
static void pool_task_wrapper(void *v) {
    pool_task_ctx_t *ctx = (pool_task_ctx_t*)v;
    if (!ctx) return;

    void *res = NULL;

    /* Execute user task. Caller is responsible for handling exceptions/crashes. */
    res = ctx->fn(ctx->arg);

    /* Try to fulfill the promise. If it fails, free the result via dtor to avoid leaks. */
    if (ol_promise_fulfill(ctx->promise, res, ctx->dtor) != 0) {
        if (res && ctx->dtor) ctx->dtor(res);
    }

    /* Release the promise handle owned by this wrapper and free context. */
    ol_promise_destroy(ctx->promise);
    free(ctx);
}

/**
 * @brief Run a task on a parallel pool and return a future for its result.
 *
 * This function creates a promise/future pair, packages the promise and task into a small
 * context, and submits a wrapper to the pool. The wrapper executes the task and resolves
 * the promise. The returned future must be destroyed by the caller.
 *
 * Returns NULL on allocation failure or if pool submission fails.
 *
 * @param pool Thread pool to submit to (must be non-NULL)
 * @param fn Task function to execute on a worker thread (must be non-NULL)
 * @param arg Argument forwarded to fn (may be NULL)
 * @param dtor Destructor for the returned value (may be NULL)
 * @return ol_future_t* Future representing the eventual result, or NULL on error
 */
ol_future_t* ol_async_run(ol_parallel_pool_t *pool, ol_async_task_fn fn, void *arg, ol_value_destructor dtor) {
    if (!pool || !fn) return NULL;

    /* Create promise/future pair. Promise is created without an event loop. */
    ol_promise_t *p = ol_promise_create(NULL);
    if (!p) return NULL;
    ol_future_t *f = ol_promise_get_future(p);
    if (!f) { ol_promise_destroy(p); return NULL; }

    /* Allocate context for pool worker */
    pool_task_ctx_t *ctx = (pool_task_ctx_t*)calloc(1, sizeof(pool_task_ctx_t));
    if (!ctx) { ol_future_destroy(f); ol_promise_destroy(p); return NULL; }

    ctx->promise = p; /* wrapper will destroy promise */
    ctx->fn = fn;
    ctx->arg = arg;
    ctx->dtor = dtor;

    /* Submit wrapper to pool. On failure, clean up and return NULL. */
    if (ol_parallel_submit(pool, pool_task_wrapper, ctx) != 0) {
        free(ctx);
        ol_future_destroy(f);
        ol_promise_destroy(p);
        return NULL;
    }

    return f;
}

/* ---------------------------------------------------------------------------
 * Event-loop scheduling
 *
 * Implementation notes:
 * - We schedule a one-shot timer with a very short deadline to post a task onto the loop.
 * - The loop callback (loop_task_trampoline) runs on the loop thread and may:
 *     * return a non-NULL pointer -> we treat it as the immediate result and fulfill the promise
 *     * return NULL -> the callback will use the provided promise to fulfill/reject later
 * - The loop_task_ctx_t is freed by the trampoline after resolution.
 * ------------------------------------------------------------------------- */

/**
 * @struct loop_task_ctx_t
 * @brief Control block for a task scheduled on the event loop.
 *
 * Fields:
 * - promise: promise owned by this context until trampoline runs
 * - cb: callback to invoke on the loop thread
 * - arg: argument forwarded to cb
 * - dtor: destructor for callback result (optional)
 * - timer_id: registration id returned by ol_event_loop_register_timer (0 if not registered)
 *
 * Ownership:
 * - The trampoline frees this structure after running and cleaning up.
 */
typedef struct loop_task_ctx {
    ol_promise_t *promise;
    ol_async_loop_fn cb;
    void *arg;
    ol_value_destructor dtor;
    uint64_t timer_id;
} loop_task_ctx_t;

/**
 * @brief Event-loop trampoline invoked by the loop timer.
 *
 * - Calls the user-provided cb on the loop thread.
 * - If cb returns a non-NULL pointer, the trampoline attempts to fulfill the promise
 *   with that value and the provided destructor.
 * - If fulfillment fails, the destructor is invoked to avoid leaks.
 * - The trampoline destroys the promise handle it owns, unregisters the timer (best-effort),
 *   and frees the control block.
 *
 * @param loop Event loop instance (non-NULL)
 * @param type Event type (ignored)
 * @param fd File descriptor (ignored)
 * @param user_data loop_task_ctx_t* control block
 */
static void loop_task_trampoline(ol_event_loop_t *loop, ol_ev_type_t type, int fd, void *user_data) {
    (void)type; (void)fd;
    loop_task_ctx_t *ctx = (loop_task_ctx_t*)user_data;
    if (!ctx) return;

    /* Invoke user callback inside loop thread. Callback may use ctx->promise directly. */
    void *ret = NULL;
    if (ctx->cb) {
        ret = ctx->cb(loop, ctx->arg, ctx->promise);
    }

    /* If callback returned a result, fulfill the promise automatically. */
    if (ret != NULL) {
        if (ol_promise_fulfill(ctx->promise, ret, ctx->dtor) != 0) {
            if (ctx->dtor) ctx->dtor(ret);
        }
    }

    /* Destroy the promise handle owned by this context. */
    ol_promise_destroy(ctx->promise);

    /* Unregister timer if present (best-effort). */
    if (ctx->timer_id != 0) {
        (void)ol_event_loop_unregister(loop, ctx->timer_id);
        ctx->timer_id = 0;
    }

    /* Free control block. */
    free(ctx);
}

/**
 * @brief Schedule a callback to run on the event loop thread and return a future.
 *
 * The callback signature allows either immediate return of a result (non-NULL) or
 * asynchronous resolution via the provided promise handle. The returned future must
 * be destroyed by the caller.
 *
 * @param loop Event loop (must be non-NULL)
 * @param cb Callback to run on loop thread (must be non-NULL)
 * @param arg Argument forwarded to cb (may be NULL)
 * @param dtor Destructor for returned value if cb returns non-NULL (may be NULL)
 * @return ol_future_t* Future representing eventual result, or NULL on error
 */
ol_future_t* ol_async_run_on_loop(ol_event_loop_t *loop, ol_async_loop_fn cb, void *arg, ol_value_destructor dtor) {
    if (!loop || !cb) return NULL;

    /* Create promise bound to the loop so continuations can wake the loop if needed. */
    ol_promise_t *p = ol_promise_create(loop);
    if (!p) return NULL;
    ol_future_t *f = ol_promise_get_future(p);
    if (!f) { ol_promise_destroy(p); return NULL; }

    loop_task_ctx_t *ctx = (loop_task_ctx_t*)calloc(1, sizeof(loop_task_ctx_t));
    if (!ctx) { ol_future_destroy(f); ol_promise_destroy(p); return NULL; }

    ctx->promise = p; /* owned by ctx until trampoline runs */
    ctx->cb = cb;
    ctx->arg = arg;
    ctx->dtor = dtor;
    ctx->timer_id = 0;

    /* Schedule via a one-shot immediate timer. Use a tiny positive offset to ensure the loop polls. */
    ol_deadline_t dl = ol_deadline_from_ns(1); /* fire as soon as loop polls */
    uint64_t id = ol_event_loop_register_timer(loop, dl, 0, loop_task_trampoline, ctx);
    if (id == 0) {
        free(ctx);
        ol_future_destroy(f);
        ol_promise_destroy(p);
        return NULL;
    }
    ctx->timer_id = id;
    return f;
}