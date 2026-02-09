#include "ol_reactive.h"

#include <stdlib.h>
#include <string.h>

/* Internal state */

struct ol_observable {
    ol_event_loop_t *loop;

    /* Subscribers */
    ol_rx_subscription_t *subs_head;

    /* Buffered items (for backpressure) */
    rx_item_node_t *q_head;
    rx_item_node_t *q_tail;
    size_t          q_size;

    /* State */
    rx_state_t state;
    int        error_code;
    bool       owns_items;
    ol_item_destructor dtor;

    /* Operator wiring */
    ol_observable_t *src_a;
    ol_observable_t *src_b;
    void (*on_src_next)(ol_observable_t *self, const void *item);
    void (*on_src_error)(ol_observable_t *self, int code);
    void (*on_src_complete)(ol_observable_t *self);

    union {
        op_ctx_map_t      map;
        op_ctx_filter_t   filter;
        op_ctx_take_t     take;
        op_ctx_debounce_t debounce;
        fd_ctx_t          fd;
    } op;

    /* Sync */
    ol_mutex_t mu;
};

struct ol_subject {
    /* Subject is an observable that users drive via on_next/on_error/on_complete */
    ol_observable_t base;
};

/* Utilities */

static void q_enqueue(ol_observable_t *o, void *item) {
    rx_item_node_t *n = (rx_item_node_t*)malloc(sizeof(rx_item_node_t));
    n->item = item;
    n->next = NULL;
    if (!o->q_tail) {
        o->q_head = o->q_tail = n;
    } else {
        o->q_tail->next = n;
        o->q_tail = n;
    }
    o->q_size++;
}

static void* q_dequeue(ol_observable_t *o) {
    rx_item_node_t *n = o->q_head;
    if (!n) return NULL;
    o->q_head = n->next;
    if (!o->q_head) o->q_tail = NULL;
    o->q_size--;
    void *item = n->item;
    free(n);
    return item;
}

static void q_clear(ol_observable_t *o) {
    while (o->q_head) {
        rx_item_node_t *n = o->q_head;
        o->q_head = n->next;
        if (o->owns_items && o->dtor) o->dtor(n->item);
        free(n);
    }
    o->q_tail = NULL;
    o->q_size = 0;
}

static void deliver_one(ol_rx_subscription_t *sub, void *item) {
    if (!sub || sub->unsubscribed) return;
    if (sub->demand == 0) return;
    sub->demand--;
    if (sub->on_next) sub->on_next(item, sub->user_data);
}

static void broadcast_error(ol_observable_t *o, int code) {
    for (ol_rx_subscription_t *s = o->subs_head; s; s = s->next) {
        if (!s->unsubscribed && s->on_error) s->on_error(code, s->user_data);
    }
}

static void broadcast_complete(ol_observable_t *o) {
    for (ol_rx_subscription_t *s = o->subs_head; s; s = s->next) {
        if (!s->unsubscribed && s->on_complete) s->on_complete(s->user_data);
    }
}

/* Event loop callbacks */

static void loop_cb_io(ol_event_loop_t *loop, ol_ev_type_t type, int fd, void *user_data) {
    (void)loop; (void)type; (void)fd;
    ol_observable_t *o = (ol_observable_t*)user_data;
    if (!o) return;
    /* Emit a NULL sentinel */
    ol_subject_on_next((ol_subject_t*)o, NULL);
}

static void loop_cb_timer(ol_event_loop_t *loop, ol_ev_type_t type, int fd, void *user_data) {
    (void)loop; (void)type; (void)fd;
    ol_observable_t *o = (ol_observable_t*)user_data;
    if (!o) return;

    ol_mutex_lock(&o->mu);
    /* If this is a pure timer source (no operator wiring and no sources), emit NULL */
    bool is_timer_source = (o->on_src_next == NULL && o->src_a == NULL && o->src_b == NULL);
    if (is_timer_source) {
        ol_mutex_unlock(&o->mu);
        ol_subject_on_next((ol_subject_t*)o, NULL);
        return;
    }

    /* Debounce */
    if (o->op.debounce.have_pending) {
        void *emit_item = o->op.debounce.last_item;
        o->op.debounce.last_item = NULL;
        o->op.debounce.have_pending = false;
        ol_mutex_unlock(&o->mu);
        ol_subject_on_next((ol_subject_t*)o, emit_item);
    } else {
        ol_mutex_unlock(&o->mu);
    }
}

/* Construction */

static ol_observable_t* observable_alloc(ol_event_loop_t *loop, ol_item_destructor dtor) {
    ol_observable_t *o = (ol_observable_t*)calloc(1, sizeof(ol_observable_t));
    if (!o) return NULL;

    o->loop = loop;
    o->subs_head = NULL;
    o->q_head = o->q_tail = NULL;
    o->q_size = 0;
    o->state = RX_PENDING;
    o->error_code = 0;
    o->owns_items = (dtor != NULL);
    o->dtor = dtor;
    o->src_a = o->src_b = NULL;
    o->on_src_next = NULL;
    o->on_src_error = NULL;
    o->on_src_complete = NULL;
    memset(&o->op, 0, sizeof(o->op));
    ol_mutex_init(&o->mu);
    return o;
}

ol_subject_t* ol_subject_create(ol_event_loop_t *loop, ol_item_destructor dtor) {
    ol_subject_t *s = (ol_subject_t*)calloc(1, sizeof(ol_subject_t));
    if (!s) return NULL;
    ol_observable_t *o = observable_alloc(loop, dtor);
    if (!o) { free(s); return NULL; }
    /* Place observable storage inside subject object for single allocation if desired.
     * Here we embed by copying; but to keep semantics correct, we keep subject as owning 'base'. */
    s->base = *o;
    free(o);
    return s;
}

void ol_subject_destroy(ol_subject_t *s) {
    if (!s) return;

    ol_observable_t *o = &s->base;

    ol_mutex_lock(&o->mu);
    /* Unregister IO callback if present */
    if (o->on_src_next == NULL && o->src_a == NULL && o->src_b == NULL && o->op.fd.fd > 0 && o->op.fd.reg_id != 0) {
        (void)ol_event_loop_unregister(o->loop, o->op.fd.reg_id);
        o->op.fd.reg_id = 0;
    }
    if (o->on_src_next && o->src_a && o->op.debounce.timer_id != 0) {
        (void)ol_event_loop_unregister(o->loop, o->op.debounce.timer_id);
        o->op.debounce.timer_id = 0;
    }
    /* Complete if not already */
    if (o->state == RX_PENDING) {
        o->state = RX_COMPLETE;
        broadcast_complete(o);
    } else if (o->state == RX_ERROR) {
        broadcast_error(o, o->error_code);
    }
    q_clear(o);
    for (ol_rx_subscription_t *sub = o->subs_head; sub; sub = sub->next) sub->unsubscribed = true;
    ol_mutex_unlock(&o->mu);

    ol_mutex_destroy(&o->mu);
    free(s);
}

ol_observable_t* ol_subject_as_observable(ol_subject_t *s) {
    return s ? &s->base : NULL;
}

ol_observable_t* ol_observable_create(ol_event_loop_t *loop, ol_item_destructor dtor) {
    return observable_alloc(loop, dtor);
}

void ol_observable_destroy(ol_observable_t *o) {
    if (!o) return;

    ol_mutex_lock(&o->mu);
    if (o->state == RX_PENDING) {
        o->state = RX_COMPLETE;
        broadcast_complete(o);
    } else if (o->state == RX_ERROR) {
        broadcast_error(o, o->error_code);
    }
    /* unregister timer/io if any */
    if (o->on_src_next && o->src_a && o->op.debounce.timer_id != 0) {
        (void)ol_event_loop_unregister(o->loop, o->op.debounce.timer_id);
        o->op.debounce.timer_id = 0;
    }
    if (o->on_src_next == NULL && o->src_a == NULL && o->src_b == NULL && o->op.fd.reg_id != 0) {
        (void)ol_event_loop_unregister(o->loop, o->op.fd.reg_id);
        o->op.fd.reg_id = 0;
    }
    q_clear(o);
    for (ol_rx_subscription_t *sub = o->subs_head; sub; sub = sub->next) sub->unsubscribed = true;
    ol_mutex_unlock(&o->mu);

    ol_mutex_destroy(&o->mu);
    free(o);
}

/* Subject emissions */

int ol_subject_on_next(ol_subject_t *s, void *item) {
    if (!s) return -1;
    ol_observable_t *o = &s->base;

    /* Operator-wired? route through operator */
    if (o->on_src_next) {
        o->on_src_next(o, item);
        return 0;
    }

    ol_mutex_lock(&o->mu);
    if (o->state != RX_PENDING) {
        ol_mutex_unlock(&o->mu);
        if (o->owns_items && o->dtor) o->dtor(item);
        return -1;
    }

    bool buffered = true;
    for (ol_rx_subscription_t *sub = o->subs_head; sub; sub = sub->next) {
        if (sub->unsubscribed) continue;
        if (sub->demand > 0) {
            sub->demand--;
            buffered = false;
            ol_rx_on_next fn = sub->on_next;
            void *ud = sub->user_data;
            ol_mutex_unlock(&o->mu);
            if (fn) fn(item, ud);
            ol_mutex_lock(&o->mu);
        }
    }
    if (buffered) {
        q_enqueue(o, item);
    } else if (o->owns_items && o->dtor) {
        o->dtor(item);
    }

    ol_mutex_unlock(&o->mu);
    return 0;
}

int ol_subject_on_error(ol_subject_t *s, int error_code) {
    if (!s) return -1;
    ol_observable_t *o = &s->base;
    ol_mutex_lock(&o->mu);
    if (o->state != RX_PENDING) { ol_mutex_unlock(&o->mu); return -1; }
    o->state = RX_ERROR;
    o->error_code = error_code;
    q_clear(o);
    ol_mutex_unlock(&o->mu);
    broadcast_error(o, error_code);
    return 0;
}

int ol_subject_on_complete(ol_subject_t *s) {
    if (!s) return -1;
    ol_observable_t *o = &s->base;
    ol_mutex_lock(&o->mu);
    if (o->state != RX_PENDING) { ol_mutex_unlock(&o->mu); return -1; }
    o->state = RX_COMPLETE;
    q_clear(o);
    ol_mutex_unlock(&o->mu);
    broadcast_complete(o);
    return 0;
}

/* Subscriptions */

ol_rx_subscription_t* ol_observable_subscribe(
    ol_observable_t *o,
    ol_rx_on_next on_next,
    ol_rx_on_error on_error,
    ol_rx_on_complete on_complete,
    size_t demand,
    void *user_data
) {
    if (!o) return NULL;
    ol_rx_subscription_t *sub = (ol_rx_subscription_t*)calloc(1, sizeof(ol_rx_subscription_t));
    if (!sub) return NULL;

    sub->parent = o;
    sub->on_next = on_next;
    sub->on_error = on_error;
    sub->on_complete = on_complete;
    sub->user_data = user_data;
    sub->demand = demand;
    sub->unsubscribed = false;

    ol_mutex_lock(&o->mu);
    sub->next = o->subs_head;
    o->subs_head = sub;

    rx_state_t st = o->state;
    int err = o->error_code;

    /* Drain buffered respecting demand */
    while (sub->demand > 0 && o->q_size > 0) {
        void *item = q_dequeue(o);
        ol_mutex_unlock(&o->mu);
        deliver_one(sub, item);
        if (o->owns_items && o->dtor) o->dtor(item);
        ol_mutex_lock(&o->mu);
    }
    ol_mutex_unlock(&o->mu);

    if (st == RX_COMPLETE && on_complete) on_complete(user_data);
    if (st == RX_ERROR    && on_error)    on_error(err, user_data);

    return sub;
}

int ol_rx_request(ol_rx_subscription_t *sub, size_t n) {
    if (!sub || sub->unsubscribed) return -1;
    if (n == 0) return 0;
    ol_observable_t *o = sub->parent;

    ol_mutex_lock(&o->mu);
    sub->demand += n;

    while (sub->demand > 0 && o->q_size > 0) {
        void *item = q_dequeue(o);
        ol_mutex_unlock(&o->mu);
        deliver_one(sub, item);
        if (o->owns_items && o->dtor) o->dtor(item);
        ol_mutex_lock(&o->mu);
    }

    ol_mutex_unlock(&o->mu);
    return 0;
}

int ol_rx_unsubscribe(ol_rx_subscription_t *sub) {
    if (!sub) return -1;
    sub->unsubscribed = true;
    return 0;
}

void ol_rx_subscription_destroy(ol_rx_subscription_t *sub) {
    if (!sub) return;
    ol_observable_t *o = sub->parent;
    if (o) {
        ol_mutex_lock(&o->mu);
        ol_rx_subscription_t **pp = &o->subs_head;
        while (*pp) {
            if (*pp == sub) { *pp = sub->next; break; }
            pp = &(*pp)->next;
        }
        ol_mutex_unlock(&o->mu);
    }
    free(sub);
}

/* Operators */

/* Map */
static void op_map_on_next(ol_observable_t *self, const void *item) {
    void *mapped = self->op.map.fn(item, self->op.map.user_data);
    ol_subject_on_next((ol_subject_t*)self, mapped);
}
static void op_passthrough_error(ol_observable_t *self, int code) { (void)self; (void)code; }
static void op_passthrough_complete(ol_observable_t *self) { (void)self; }

ol_observable_t* ol_rx_map(ol_observable_t *src, ol_rx_map_fn fn, void *user_data, ol_item_destructor out_dtor) {
    if (!src || !fn) return NULL;
    ol_observable_t *o = observable_alloc(src->loop, out_dtor);
    if (!o) return NULL;
    o->src_a = src;
    o->on_src_next = op_map_on_next;
    o->on_src_error = op_passthrough_error;
    o->on_src_complete = op_passthrough_complete;
    o->op.map.fn = fn;
    o->op.map.user_data = user_data;
    o->op.map.out_dtor = out_dtor;
    o->owns_items = (out_dtor != NULL);
    return o;
}

/* Filter */
static void op_filter_on_next(ol_observable_t *self, const void *item) {
    if (self->op.filter.pred(item, self->op.filter.user_data)) {
        ol_subject_on_next((ol_subject_t*)self, (void*)item);
    } else {
        if (self->owns_items && self->dtor) self->dtor((void*)item);
    }
}
ol_observable_t* ol_rx_filter(ol_observable_t *src, ol_rx_filter_fn pred, void *user_data) {
    if (!src || !pred) return NULL;
    ol_observable_t *o = observable_alloc(src->loop, src->dtor);
    if (!o) return NULL;
    o->owns_items = src->owns_items;
    o->src_a = src;
    o->on_src_next = op_filter_on_next;
    o->on_src_error = op_passthrough_error;
    o->on_src_complete = op_passthrough_complete;
    o->op.filter.pred = pred;
    o->op.filter.user_data = user_data;
    return o;
}

/* Take */
static void op_take_on_next(ol_observable_t *self, const void *item) {
    if (self->op.take.remaining == 0) {
        if (self->owns_items && self->dtor) self->dtor((void*)item);
        return;
    }
    self->op.take.remaining--;
    ol_subject_on_next((ol_subject_t*)self, (void*)item);
    if (self->op.take.remaining == 0) {
        ol_subject_on_complete((ol_subject_t*)self);
    }
}
ol_observable_t* ol_rx_take(ol_observable_t *src, size_t n) {
    if (!src || n == 0) return NULL;
    ol_observable_t *o = observable_alloc(src->loop, src->dtor);
    if (!o) return NULL;
    o->owns_items = src->owns_items;
    o->src_a = src;
    o->on_src_next = op_take_on_next;
    o->on_src_error = op_passthrough_error;
    o->on_src_complete = op_passthrough_complete;
    o->op.take.remaining = n;
    return o;
}

/* Merge */
static void op_merge_on_next(ol_observable_t *self, const void *item) {
    ol_subject_on_next((ol_subject_t*)self, (void*)item);
}
ol_observable_t* ol_rx_merge(ol_observable_t *a, ol_observable_t *b, ol_item_destructor dtor_hint) {
    if (!a || !b) return NULL;
    ol_observable_t *o = observable_alloc(a->loop, dtor_hint);
    if (!o) return NULL;
    o->owns_items = (dtor_hint != NULL);
    o->src_a = a;
    o->src_b = b;
    o->on_src_next = op_merge_on_next;
    o->on_src_error = op_passthrough_error;
    o->on_src_complete = op_passthrough_complete;
    return o;
}

/* Debounce */
static void op_debounce_on_next(ol_observable_t *self, const void *item) {
    ol_mutex_lock(&self->mu);
    if (self->op.debounce.have_pending && self->owns_items && self->dtor) {
        self->dtor(self->op.debounce.last_item);
    }
    self->op.debounce.last_item = (void*)item;
    self->op.debounce.have_pending = true;
    if (self->op.debounce.timer_id == 0) {
        ol_deadline_t dl = ol_deadline_from_ns(self->op.debounce.interval_ns);
        self->op.debounce.timer_id = ol_event_loop_register_timer(self->loop, dl, 0, loop_cb_timer, self);
    }
    ol_mutex_unlock(&self->mu);
}
ol_observable_t* ol_rx_debounce(ol_observable_t *src, int64_t interval_ns) {
    if (!src || interval_ns <= 0) return NULL;
    ol_observable_t *o = observable_alloc(src->loop, src->dtor);
    if (!o) return NULL;
    o->owns_items = src->owns_items;
    o->src_a = src;
    o->on_src_next = op_debounce_on_next;
    o->on_src_error = op_passthrough_error;
    o->on_src_complete = op_passthrough_complete;
    o->op.debounce.interval_ns = interval_ns;
    o->op.debounce.timer_id = 0;
    o->op.debounce.have_pending = false;
    o->op.debounce.last_item = NULL;
    return o;
}

/* Timer source */
ol_observable_t* ol_rx_timer(ol_event_loop_t *loop, int64_t period_ns, size_t count) {
    if (!loop || period_ns <= 0) return NULL;
    ol_subject_t *s = ol_subject_create(loop, NULL);
    if (!s) return NULL;
    ol_deadline_t first = ol_deadline_from_ns(period_ns);
    uint64_t id = ol_event_loop_register_timer(loop, first, (count == 1) ? 0 : period_ns, loop_cb_timer, &s->base);
    (void)id;
    return &s->base;
}

/* IO source */
ol_observable_t* ol_rx_from_fd(ol_event_loop_t *loop, int fd, uint32_t mask) {
    if (!loop || fd < 0) return NULL;
    ol_subject_t *s = ol_subject_create(loop, NULL);
    if (!s) return NULL;

    uint64_t id = ol_event_loop_register_io(loop, fd, mask, loop_cb_io, &s->base);
    if (id == 0) { ol_subject_destroy(s); return NULL; }

    s->base.op.fd.fd = fd;
    s->base.op.fd.mask = mask;
    s->base.op.fd.reg_id = id;
    return &s->base;
}

/* Introspection */

bool ol_rx_completed(const ol_observable_t *o) {
    if (!o) return false;
    return o->state == RX_COMPLETE || o->state == RX_ERROR;
}

size_t ol_rx_subscriber_count(const ol_observable_t *o) {
    if (!o) return 0;
    size_t c = 0;
    const ol_rx_subscription_t *s = o->subs_head;
    while (s) { c++; s = s->next; }
    return c;
}
