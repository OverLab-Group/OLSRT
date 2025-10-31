/* OLSRT - Cancellation & Deadlines */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>

/* ============================== Cancellation ================================ */

typedef struct ol_cleanup_node_s {
    ol_cleanup_fn fn;
    void         *arg;
    struct ol_cleanup_node_s *next;
} ol_cleanup_node_t;

struct ol_cancel_s {
    int               trig;    /* 0 not triggered, 1 triggered */
    ol_err_t          reason;  /* trigger reason */
    ol_cleanup_node_t *subs;   /* LIFO list of cleanup callbacks */
};

ol_cancel_t* ol_cancel_create(void) {
    return (ol_cancel_t*)calloc(1, sizeof(ol_cancel_t));
}

int ol_cancel_register(ol_cancel_t *c, ol_cleanup_fn fn, void *arg) {
    if (!c || !fn) return OL_ERR_STATE;
    ol_cleanup_node_t *n = (ol_cleanup_node_t*)malloc(sizeof(*n));
    if (!n) return OL_ERR_ALLOC;
    n->fn   = fn;
    n->arg  = arg;
    n->next = c->subs;
    c->subs = n;
    return OL_OK;
}

int ol_cancel_trigger(ol_cancel_t *c, ol_err_t reason) {
    if (!c) return OL_ERR_STATE;
    if (c->trig) return OL_OK; /* idempotent */
    c->trig   = 1;
    c->reason = reason;
    for (ol_cleanup_node_t *n = c->subs; n; n = n->next) {
        n->fn(n->arg);
    }
    return OL_OK;
}

int ol_cancel_is_triggered(ol_cancel_t *c) {
    return c ? c->trig : 0;
}

ol_err_t ol_cancel_reason(ol_cancel_t *c) {
    return c ? c->reason : OL_ERR_GENERIC;
}

/* ================================= Deadlines ================================= */

static inline ol_deadline_t ol_deadline_from_now(uint64_t delta_ms) {
    ol_deadline_t d;
    d.at_ms = ol_monotonic_ms() + delta_ms;
    return d;
}

/* Check if deadline has expired (now >= at_ms). Returns 1 if expired, 0 otherwise. */
static inline int ol_deadline_expired(const ol_deadline_t *d) {
    if (!d) return 1;
    return (ol_monotonic_ms() >= d->at_ms) ? 1 : 0;
}

/* Compute remaining time until deadline in milliseconds.
 * Returns 0 if already expired. */
static inline uint64_t ol_deadline_remaining_ms(const ol_deadline_t *d) {
    if (!d) return 0;
    uint64_t now = ol_monotonic_ms();
    return (now >= d->at_ms) ? 0 : (d->at_ms - now);
}
