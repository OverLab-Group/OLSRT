/* OLSRT - Futures & Promises */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>

/* Callback node (FIFO chain) */
typedef struct ol_fut_cb_node_s {
    ol_future_cb cb;
    void        *arg;
    struct ol_fut_cb_node_s *next;
} ol_fut_cb_node_t;

/* Future object */
struct ol_future_s {
    ol_loop_t        *loop;
    ol_future_state_t st;
    void             *val;
    ol_err_t          err;
    ol_fut_cb_node_t *cbs;  /* pending callbacks */
};

/* Internal: dispatch and clear callback chain */
static void ol_future_dispatch(ol_future_t *f) {
    ol_fut_cb_node_t *n = f->cbs;
    f->cbs = NULL;
    while (n) {
        ol_fut_cb_node_t *next = n->next;
        n->cb(f, n->arg);
        free(n);
        n = next;
    }
}

/* Create a future bound to a loop */
ol_future_t* ol_future_create(ol_loop_t *loop) {
    if (!loop) return NULL;
    ol_future_t *f = (ol_future_t*)calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->loop = loop;
    f->st   = OL_FUT_PENDING;
    return f;
}

/* Register a callback; if future already settled, schedule callback on loop */
int ol_future_then(ol_future_t *f, ol_future_cb cb, void *arg) {
    if (!f || !cb) return OL_ERR_STATE;
    ol_fut_cb_node_t *n = (ol_fut_cb_node_t*)malloc(sizeof(*n));
    if (!n) return OL_ERR_ALLOC;
    n->cb = cb; n->arg = arg; n->next = NULL;

    if (f->st == OL_FUT_PENDING) {
        /* Append to callback chain */
        if (!f->cbs) {
            f->cbs = n;
        } else {
            ol_fut_cb_node_t *t = f->cbs;
            while (t->next) t = t->next;
            t->next = n;
        }
    } else {
        /* Already settled: run callback asynchronously via loop */
        ol_loop_post(f->loop, (ol_task_fn)cb, f);
        free(n);
    }
    return OL_OK;
}

/* Resolve with a value and dispatch callbacks */
int ol_future_resolve(ol_future_t *f, void *value) {
    if (!f) return OL_ERR_STATE;
    if (f->st != OL_FUT_PENDING) return OL_ERR_STATE;
    f->st  = OL_FUT_RESOLVED;
    f->val = value;
    ol_future_dispatch(f);
    return OL_OK;
}

/* Reject with an error and dispatch callbacks */
int ol_future_reject(ol_future_t *f, ol_err_t err) {
    if (!f) return OL_ERR_STATE;
    if (f->st != OL_FUT_PENDING) return OL_ERR_STATE;
    f->st  = OL_FUT_REJECTED;
    f->err = err;
    ol_future_dispatch(f);
    return OL_OK;
}

/* Cancel with a reason and dispatch callbacks */
int ol_future_cancel(ol_future_t *f, ol_err_t reason) {
    if (!f) return OL_ERR_STATE;
    if (f->st != OL_FUT_PENDING) return OL_ERR_STATE;
    f->st  = OL_FUT_CANCELED;
    f->err = reason;
    ol_future_dispatch(f);
    return OL_OK;
}

/* Accessors */
ol_future_state_t ol_future_state(ol_future_t *f) { return f ? f->st : OL_FUT_REJECTED; }
void*             ol_future_value(ol_future_t *f) { return f ? f->val : NULL; }
ol_err_t          ol_future_error(ol_future_t *f) { return f ? f->err : OL_ERR_GENERIC; }
