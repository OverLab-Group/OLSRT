/**
 * @file ol_async.c
 * @brief Asynchronous task execution primitives for OLSRT
 * @version 1.2.0
 * 
 * This module provides functions for running tasks on thread pools
 * and event loops with future/promise semantics, working across
 * all supported platforms.
 */

#include "ol_async.h"
#include "ol_common.h"
#include "ol_promise.h"
#include "ol_parallel_pool.h"
#include "ol_event_loop.h"

#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Internal structures for pool tasks
 * -------------------------------------------------------------------------- */

/**
 * @brief Pool task context
 */
typedef struct {
    ol_promise_t *promise;      /**< Promise to fulfill */
    ol_async_task_fn task_fn;   /**< User task function */
    void *arg;                  /**< Task argument */
    ol_value_destructor dtor;   /**< Result destructor */
} ol_pool_task_ctx_t;

/**
 * @brief Pool task wrapper (executed by thread pool)
 */
static void ol_pool_task_wrapper(void *arg) {
    ol_pool_task_ctx_t *ctx = (ol_pool_task_ctx_t*)arg;
    if (!ctx) {
        return;
    }
    
    /* Execute user task */
    void *result = NULL;
    if (ctx->task_fn) {
        result = ctx->task_fn(ctx->arg);
    }
    
    /* Fulfill promise */
    if (ol_promise_fulfill(ctx->promise, result, ctx->dtor) != OL_SUCCESS) {
        /* Promise already resolved: destroy result */
        if (result && ctx->dtor) {
            ctx->dtor(result);
        }
    }
    
    /* Cleanup */
    ol_promise_destroy(ctx->promise);
    free(ctx);
}

/* --------------------------------------------------------------------------
 * Internal structures for loop tasks
 * -------------------------------------------------------------------------- */

/**
 * @brief Loop task context
 */
typedef struct {
    ol_promise_t *promise;      /**< Promise to fulfill */
    ol_async_loop_fn loop_fn;   /**< User loop function */
    void *arg;                  /**< Function argument */
    ol_value_destructor dtor;   /**< Result destructor */
    uint64_t timer_id;          /**< Timer registration ID */
} ol_loop_task_ctx_t;

/**
 * @brief Loop task trampoline (executed on event loop thread)
 */
static void ol_loop_task_trampoline(ol_event_loop_t *loop,
                                    ol_ev_type_t type,
                                    int fd,
                                    void *arg) {
    (void)type;
    (void)fd;
    
    ol_loop_task_ctx_t *ctx = (ol_loop_task_ctx_t*)arg;
    if (!ctx) {
        return;
    }
    
    /* Execute user callback */
    void *result = NULL;
    if (ctx->loop_fn) {
        result = ctx->loop_fn(loop, ctx->arg, ctx->promise);
    }
    
    /* If callback returned a result, fulfill promise */
    if (result != NULL) {
        if (ol_promise_fulfill(ctx->promise, result, ctx->dtor) != OL_SUCCESS) {
            /* Promise already resolved: destroy result */
            if (result && ctx->dtor) {
                ctx->dtor(result);
            }
        }
    }
    /* Otherwise, callback is responsible for resolving promise */
    
    /* Unregister timer */
    if (ctx->timer_id != 0) {
        ol_event_loop_unregister(loop, ctx->timer_id);
    }
    
    /* Cleanup */
    ol_promise_destroy(ctx->promise);
    free(ctx);
}

/* --------------------------------------------------------------------------
 * Public API implementation
 * -------------------------------------------------------------------------- */

ol_future_t* ol_async_run(ol_parallel_pool_t *pool,
                          ol_async_task_fn fn,
                          void *arg,
                          ol_value_destructor dtor) {
    if (!pool || !fn) {
        return NULL;
    }
    
    /* Create promise/future pair */
    ol_promise_t *promise = ol_promise_create(NULL);
    if (!promise) {
        return NULL;
    }
    
    ol_future_t *future = ol_promise_get_future(promise);
    if (!future) {
        ol_promise_destroy(promise);
        return NULL;
    }
    
    /* Create task context */
    ol_pool_task_ctx_t *ctx = (ol_pool_task_ctx_t*)calloc(1, sizeof(ol_pool_task_ctx_t));
    if (!ctx) {
        ol_future_destroy(future);
        ol_promise_destroy(promise);
        return NULL;
    }
    
    ctx->promise = promise;
    ctx->task_fn = fn;
    ctx->arg = arg;
    ctx->dtor = dtor;
    
    /* Submit to thread pool */
    if (ol_parallel_submit(pool, ol_pool_task_wrapper, ctx) != OL_SUCCESS) {
        /* Submission failed */
        free(ctx);
        ol_future_destroy(future);
        ol_promise_destroy(promise);
        return NULL;
    }
    
    /* Future will be resolved by pool worker */
    return future;
}

ol_future_t* ol_async_run_on_loop(ol_event_loop_t *loop,
                                  ol_async_loop_fn cb,
                                  void *arg,
                                  ol_value_destructor dtor) {
    if (!loop || !cb) {
        return NULL;
    }
    
    /* Create promise bound to the loop */
    ol_promise_t *promise = ol_promise_create(loop);
    if (!promise) {
        return NULL;
    }
    
    ol_future_t *future = ol_promise_get_future(promise);
    if (!future) {
        ol_promise_destroy(promise);
        return NULL;
    }
    
    /* Create loop task context */
    ol_loop_task_ctx_t *ctx = (ol_loop_task_ctx_t*)calloc(1, sizeof(ol_loop_task_ctx_t));
    if (!ctx) {
        ol_future_destroy(future);
        ol_promise_destroy(promise);
        return NULL;
    }
    
    ctx->promise = promise;
    ctx->loop_fn = cb;
    ctx->arg = arg;
    ctx->dtor = dtor;
    ctx->timer_id = 0;
    
    /* Schedule on event loop with immediate timer */
    ol_deadline_t deadline = ol_deadline_from_ns(1); /* As soon as possible */
    uint64_t timer_id = ol_event_loop_register_timer(loop, deadline, 0,
                                                    ol_loop_task_trampoline,
                                                    ctx);
    if (timer_id == 0) {
        /* Registration failed */
        free(ctx);
        ol_future_destroy(future);
        ol_promise_destroy(promise);
        return NULL;
    }
    
    ctx->timer_id = timer_id;
    return future;
}