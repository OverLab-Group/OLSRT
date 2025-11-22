#include "ol_promise.h"

#include <stdlib.h>
#include <string.h>

/* Shared core between promise and futures */
typedef struct ol_cont_node {
    ol_future_cb cb;
    void *user_data;
    struct ol_cont_node *next;
} ol_cont_node_t;

typedef struct ol_core {
    /* Sync */
    ol_mutex_t mu;
    ol_cond_t  cv;

    /* Resolution state */
    ol_promise_state_t state;
    void *value;
    ol_value_destructor dtor;
    int   error_code;

    /* Ownership */
    bool value_taken;

    /* Continuations (singly-linked list) */
    ol_cont_node_t *conts;

    /* Refcount: promise + N futures */
    int refs;

    /* Optional event loop to wake on resolution */
    struct ol_event_loop *loop;
} ol_core_t;

struct ol_promise { ol_core_t *core; };
struct ol_future  { ol_core_t *core; };

/* Core helpers */

static ol_core_t* ol_core_create(struct ol_event_loop *loop) {
    ol_core_t *c = (ol_core_t*)calloc(1, sizeof(ol_core_t));
    if (!c) return NULL;
    if (ol_mutex_init(&c->mu) != 0) { free(c); return NULL; }
    if (ol_cond_init(&c->cv) != 0) { ol_mutex_destroy(&c->mu); free(c); return NULL; }
    c->state = OL_PROMISE_PENDING;
    c->value = NULL;
    c->dtor  = NULL;
    c->error_code = 0;
    c->value_taken = false;
    c->conts = NULL;
    c->refs = 1;
    c->loop = loop;
    return c;
}

static void ol_core_release(ol_core_t *c) {
    if (!c) return;

    /* Free remaining continuations (they won't run) */
    ol_cont_node_t *n = c->conts;
    while (n) { ol_cont_node_t *nx = n->next; free(n); n = nx; }

    /* Run destructor if value still owned */
    if (c->value && c->dtor && !c->value_taken) {
        c->dtor(c->value);
    }

    ol_cond_destroy(&c->cv);
    ol_mutex_destroy(&c->mu);
    free(c);
}

static void ol_core_ref(ol_core_t *c) {
    ol_mutex_lock(&c->mu);
    c->refs++;
    ol_mutex_unlock(&c->mu);
}

static void ol_core_unref(ol_core_t *c) {
    bool free_now = false;
    ol_mutex_lock(&c->mu);
    c->refs--;
    if (c->refs == 0) free_now = true;
    ol_mutex_unlock(&c->mu);
    if (free_now) ol_core_release(c);
}

static void ol_core_dispatch(ol_core_t *c) {
    /* Snapshot continuations under lock */
    ol_mutex_lock(&c->mu);
    ol_cont_node_t *list = c->conts;
    c->conts = NULL; /* consume */
    ol_promise_state_t st = c->state;
    const void *val = c->value_taken ? NULL : c->value;
    int err = c->error_code;
    struct ol_event_loop *loop = c->loop;
    ol_mutex_unlock(&c->mu);

    /* Best-effort wake loop to allow loop-bound work to progress */
    if (loop) (void)ol_event_loop_wake(loop);

    /* Invoke callbacks inline (thread where resolve occurred) */
    for (ol_cont_node_t *n = list; n; n = n->next) {
        n->cb(loop, st, val, err, n->user_data);
    }
    /* Free nodes */
    ol_cont_node_t *cur = list;
    while (cur) { ol_cont_node_t *nx = cur->next; free(cur); cur = nx; }
}

/* Public API: Promise */

ol_promise_t* ol_promise_create(struct ol_event_loop *loop) {
    ol_core_t *c = ol_core_create(loop);
    if (!c) return NULL;
    ol_promise_t *p = (ol_promise_t*)calloc(1, sizeof(ol_promise_t));
    if (!p) { ol_core_release(c); return NULL; }
    p->core = c;
    return p;
}

void ol_promise_destroy(ol_promise_t *p) {
    if (!p) return;
    if (p->core) ol_core_unref(p->core);
    free(p);
}

ol_future_t* ol_promise_get_future(ol_promise_t *p) {
    if (!p || !p->core) return NULL;
    ol_future_t *f = (ol_future_t*)calloc(1, sizeof(ol_future_t));
    if (!f) return NULL;
    ol_core_ref(p->core);
    f->core = p->core;
    return f;
}

int ol_promise_fulfill(ol_promise_t *p, void *value, ol_value_destructor dtor) {
    if (!p || !p->core) return -1;
    ol_core_t *c = p->core;

    ol_mutex_lock(&c->mu);
    if (c->state != OL_PROMISE_PENDING) {
        ol_mutex_unlock(&c->mu);
        /* Caller owns 'value'; do not leak on duplicate fulfill */
        if (dtor) dtor(value);
        return -1;
    }
    c->state = OL_PROMISE_FULFILLED;
    c->value = value;
    c->dtor  = dtor;
    ol_cond_broadcast(&c->cv);
    ol_mutex_unlock(&c->mu);

    ol_core_dispatch(c);
    return 0;
}

int ol_promise_reject(ol_promise_t *p, int error_code) {
    if (!p || !p->core) return -1;
    ol_core_t *c = p->core;

    ol_mutex_lock(&c->mu);
    if (c->state != OL_PROMISE_PENDING) {
        ol_mutex_unlock(&c->mu);
        return -1;
    }
    c->state = OL_PROMISE_REJECTED;
    c->error_code = error_code;
    ol_cond_broadcast(&c->cv);
    ol_mutex_unlock(&c->mu);

    ol_core_dispatch(c);
    return 0;
}

int ol_promise_cancel(ol_promise_t *p) {
    if (!p || !p->core) return -1;
    ol_core_t *c = p->core;

    ol_mutex_lock(&c->mu);
    if (c->state != OL_PROMISE_PENDING) {
        ol_mutex_unlock(&c->mu);
        return -1;
    }
    c->state = OL_PROMISE_CANCELED;
    ol_cond_broadcast(&c->cv);
    ol_mutex_unlock(&c->mu);

    ol_core_dispatch(c);
    return 0;
}

ol_promise_state_t ol_promise_state(const ol_promise_t *p) {
    if (!p || !p->core) return OL_PROMISE_CANCELED;
    ol_promise_state_t st;
    ol_mutex_lock((ol_mutex_t*)&p->core->mu);
    st = p->core->state;
    ol_mutex_unlock((ol_mutex_t*)&p->core->mu);
    return st;
}

bool ol_promise_is_done(const ol_promise_t *p) {
    return ol_promise_state(p) != OL_PROMISE_PENDING;
}

/* Public API: Future */

void ol_future_destroy(ol_future_t *f) {
    if (!f) return;
    if (f->core) ol_core_unref(f->core);
    free(f);
}

int ol_future_await(ol_future_t *f, int64_t deadline_ns) {
    if (!f || !f->core) return -1;
    ol_core_t *c = f->core;

    ol_mutex_lock(&c->mu);
    while (c->state == OL_PROMISE_PENDING) {
        int r = ol_cond_wait_until(&c->cv, &c->mu, deadline_ns);
        if (r == 1) {
            /* Signaled; loop re-checks state to handle spurious wakeups */
            continue;
        } else {
            ol_mutex_unlock(&c->mu);
            return r; /* 0 timeout, -1 error */
        }
    }
    ol_mutex_unlock(&c->mu);
    return 1;
}

int ol_future_then(ol_future_t *f, ol_future_cb cb, void *user_data) {
    if (!f || !f->core || !cb) return -1;
    ol_core_t *c = f->core;

    ol_mutex_lock(&c->mu);
    if (c->state == OL_PROMISE_PENDING) {
        ol_cont_node_t *n = (ol_cont_node_t*)calloc(1, sizeof(ol_cont_node_t));
        if (!n) { ol_mutex_unlock(&c->mu); return -1; }
        n->cb = cb;
        n->user_data = user_data;
        n->next = c->conts;
        c->conts = n;
        ol_mutex_unlock(&c->mu);
        return 0;
    } else {
        /* Already complete: invoke outside lock */
        ol_promise_state_t st = c->state;
        const void *val = c->value_taken ? NULL : c->value;
        int err = c->error_code;
        struct ol_event_loop *loop = c->loop;
        ol_mutex_unlock(&c->mu);

        if (loop) (void)ol_event_loop_wake(loop);
        cb(loop, st, val, err, user_data);
        return 0;
    }
}

const void* ol_future_get_value_const(const ol_future_t *f) {
    if (!f || !f->core) return NULL;
    const void *ptr = NULL;
    ol_mutex_lock((ol_mutex_t*)&f->core->mu);
    if (f->core->state == OL_PROMISE_FULFILLED && !f->core->value_taken) {
        ptr = f->core->value;
    }
    ol_mutex_unlock((ol_mutex_t*)&f->core->mu);
    return ptr;
}

void* ol_future_take_value(ol_future_t *f) {
    if (!f || !f->core) return NULL;
    void *ptr = NULL;
    ol_mutex_lock((ol_mutex_t*)&f->core->mu);
    if (f->core->state == OL_PROMISE_FULFILLED && !f->core->value_taken) {
        ptr = f->core->value;
        f->core->value = NULL;
        f->core->value_taken = true;
        /* Prevent destructor from running later */
        f->core->dtor = NULL;
    }
    ol_mutex_unlock((ol_mutex_t*)&f->core->mu);
    return ptr;
}

int ol_future_error(const ol_future_t *f) {
    if (!f || !f->core) return 0;
    int err = 0;
    ol_mutex_lock((ol_mutex_t*)&f->core->mu);
    if (f->core->state == OL_PROMISE_REJECTED) {
        err = f->core->error_code;
    }
    ol_mutex_unlock((ol_mutex_t*)&f->core->mu);
    return err;
}

ol_promise_state_t ol_future_state(const ol_future_t *f) {
    if (!f || !f->core) return OL_PROMISE_CANCELED;
    ol_promise_state_t st;
    ol_mutex_lock((ol_mutex_t*)&f->core->mu);
    st = f->core->state;
    ol_mutex_unlock((ol_mutex_t*)&f->core->mu);
    return st;
}
