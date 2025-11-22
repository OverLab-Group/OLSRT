#include "ol_streams.h"
#include "ol_lock_mutex.h"

#include <stdlib.h>
#include <string.h>

/* Internal types */

typedef enum {
    S_PENDING = 0,
    S_ERROR,
    S_COMPLETED
} s_state_t;

typedef struct s_item_node {
    void *item;
    struct s_item_node *next;
} s_item_node_t;

struct ol_subscription {
    ol_stream_t *parent;
    ol_on_next_fn     on_next;
    ol_on_error_fn    on_error;
    ol_on_complete_fn on_complete;
    void *user_data;

    size_t demand;      /* requested but not yet delivered */
    bool   unsubscribed;

    struct ol_subscription *next;
};

typedef struct op_ctx_map {
    ol_map_fn fn;
    void *user_data;
    ol_item_destructor out_dtor;
} op_ctx_map_t;

typedef struct op_ctx_filter {
    ol_filter_fn pred;
    void *user_data;
} op_ctx_filter_t;

typedef struct op_ctx_take {
    size_t remaining;
} op_ctx_take_t;

typedef struct op_ctx_debounce {
    int64_t interval_ns;
    uint64_t timer_id;
    bool     have_pending;
    void    *last_item;
} op_ctx_debounce_t;

typedef struct fd_ctx {
    int fd;
    uint32_t mask;
    uint64_t reg_id;
} fd_ctx_t;

struct ol_stream {
    ol_event_loop_t *loop;

    /* Subscriptions list */
    ol_subscription_t *subs_head;

    /* Buffered items (for backpressure) */
    s_item_node_t *q_head;
    s_item_node_t *q_tail;
    size_t         q_size;

    /* State */
    s_state_t state;
    int       error_code;
    bool      owns_items; /* indicates items should be destroyed with dtor */
    ol_item_destructor dtor;

    /* Operator wiring */
    ol_stream_t *src_a;
    ol_stream_t *src_b;
    void (*on_src_next)(ol_stream_t *self, const void *item);
    void (*on_src_error)(ol_stream_t *self, int code);
    void (*on_src_complete)(ol_stream_t *self);

    /* Operator contexts */
    union {
        op_ctx_map_t      map;
        op_ctx_filter_t   filter;
        op_ctx_take_t     take;
        op_ctx_debounce_t debounce;
        fd_ctx_t          fd;
    } op;

    /* Synchronization around queue & subs */
    ol_mutex_t mu;
};

/* Utilities */

static void stream_enqueue(ol_stream_t *s, void *item) {
    s_item_node_t *n = (s_item_node_t*)malloc(sizeof(s_item_node_t));
    n->item = item;
    n->next = NULL;
    if (!s->q_tail) {
        s->q_head = s->q_tail = n;
    } else {
        s->q_tail->next = n;
        s->q_tail = n;
    }
    s->q_size++;
}

static void* stream_dequeue(ol_stream_t *s) {
    s_item_node_t *n = s->q_head;
    if (!n) return NULL;
    s->q_head = n->next;
    if (!s->q_head) s->q_tail = NULL;
    s->q_size--;
    void *item = n->item;
    free(n);
    return item;
}

static void stream_clear_queue(ol_stream_t *s) {
    while (s->q_head) {
        s_item_node_t *n = s->q_head;
        s->q_head = n->next;
        if (s->owns_items && s->dtor) s->dtor(n->item);
        free(n);
    }
    s->q_tail = NULL;
    s->q_size = 0;
}

static void deliver_to_sub(ol_subscription_t *sub, void *item) {
    if (!sub || sub->unsubscribed) return;
    if (sub->demand == 0) return; /* backpressure: wait until requested */
    sub->demand--;
    if (sub->on_next) sub->on_next(item, sub->user_data);
}

static void broadcast_error(ol_stream_t *s, int code) {
    for (ol_subscription_t *sub = s->subs_head; sub; sub = sub->next) {
        if (!sub->unsubscribed && sub->on_error) sub->on_error(code, sub->user_data);
    }
}

static void broadcast_complete(ol_stream_t *s) {
    for (ol_subscription_t *sub = s->subs_head; sub; sub = sub->next) {
        if (!sub->unsubscribed && sub->on_complete) sub->on_complete(sub->user_data);
    }
}

/* Event loop callbacks */

static void loop_cb_io(ol_event_loop_t *loop, ol_ev_type_t type, int fd, void *user_data) {
    (void)loop; (void)type;
    ol_stream_t *s = (ol_stream_t*)user_data;
    if (!s) return;

    /* Emit a NULL sentinel for IO readiness */
    ol_stream_emit_next(s, NULL);
}

static void loop_cb_timer(ol_event_loop_t *loop, ol_ev_type_t type, int fd, void *user_data) {
    (void)loop; (void)type; (void)fd;
    ol_stream_t *s = (ol_stream_t*)user_data;
    if (!s) return;

    /* For debounce: if have pending, emit last_item and clear; otherwise for timer source emit NULL */
    ol_mutex_lock(&s->mu);
    if (s->on_src_next == NULL && s->src_a == NULL) {
        /* timer source stream */
        ol_mutex_unlock(&s->mu);
        ol_stream_emit_next(s, NULL);
        return;
    }

    /* debounce */
    if (s->op.debounce.have_pending) {
        void *emit_item = s->op.debounce.last_item;
        s->op.debounce.last_item = NULL;
        s->op.debounce.have_pending = false;
        ol_mutex_unlock(&s->mu);
        ol_stream_emit_next(s, emit_item);
    } else {
        ol_mutex_unlock(&s->mu);
    }
}

/* Construction & destruction */

ol_stream_t* ol_stream_create(ol_event_loop_t *loop, ol_item_destructor dtor) {
    if (!loop) return NULL;
    ol_stream_t *s = (ol_stream_t*)calloc(1, sizeof(ol_stream_t));
    if (!s) return NULL;

    s->loop = loop;
    s->subs_head = NULL;
    s->q_head = s->q_tail = NULL;
    s->q_size = 0;
    s->state = S_PENDING;
    s->error_code = 0;
    s->owns_items = (dtor != NULL);
    s->dtor = dtor;
    s->src_a = s->src_b = NULL;
    s->on_src_next = NULL;
    s->on_src_error = NULL;
    s->on_src_complete = NULL;
    memset(&s->op, 0, sizeof(s->op));
    ol_mutex_init(&s->mu);
    return s;
}

void ol_stream_destroy(ol_stream_t *s) {
    if (!s) return;

    ol_mutex_lock(&s->mu);

    /* If from_fd: unregister fd */
    if (s->on_src_next == NULL && s->src_a == NULL && s->src_b == NULL && s->op.fd.fd > 0 && s->op.fd.reg_id != 0) {
        (void)ol_event_loop_unregister(s->loop, s->op.fd.reg_id);
        s->op.fd.reg_id = 0;
    }

    /* If debounce timer registered, clear it */
    if (s->on_src_next && s->src_a && s->op.debounce.timer_id != 0) {
        (void)ol_event_loop_unregister(s->loop, s->op.debounce.timer_id);
        s->op.debounce.timer_id = 0;
    }

    /* mark completion if not already done to inform subscribers */
    if (s->state == S_PENDING) {
        s->state = S_COMPLETED;
        broadcast_complete(s);
    } else if (s->state == S_ERROR) {
        broadcast_error(s, s->error_code);
    }

    /* clear queue and mark subscribers unsubscribed */
    stream_clear_queue(s);
    for (ol_subscription_t *sub = s->subs_head; sub; sub = sub->next) {
        sub->unsubscribed = true;
    }

    ol_mutex_unlock(&s->mu);
    ol_mutex_destroy(&s->mu);
    free(s);
}

/* Subscriptions */

ol_subscription_t* ol_stream_subscribe(
    ol_stream_t *s,
    ol_on_next_fn on_next,
    ol_on_error_fn on_error,
    ol_on_complete_fn on_complete,
    size_t demand,
    void *user_data
) {
    if (!s) return NULL;
    ol_subscription_t *sub = (ol_subscription_t*)calloc(1, sizeof(ol_subscription_t));
    if (!sub) return NULL;

    sub->parent = s;
    sub->on_next = on_next;
    sub->on_error = on_error;
    sub->on_complete = on_complete;
    sub->user_data = user_data;
    sub->demand = demand;
    sub->unsubscribed = false;

    ol_mutex_lock(&s->mu);
    /* Append to subs list */
    sub->next = s->subs_head;
    s->subs_head = sub;

    /* If stream already completed or errored, notify immediately */
    s_state_t st = s->state;
    int err = s->error_code;

    /* Deliver buffered items respecting demand */
    while (sub->demand > 0 && s->q_size > 0) {
        void *item = stream_dequeue(s);
        /* Deliver outside of lock to avoid user re-entry deadlocks */
        ol_mutex_unlock(&s->mu);
        deliver_to_sub(sub, item);
        if (s->owns_items && s->dtor) s->dtor(item);
        ol_mutex_lock(&s->mu);
    }

    ol_mutex_unlock(&s->mu);

    if (st == S_COMPLETED && on_complete) on_complete(user_data);
    if (st == S_ERROR && on_error) on_error(err, user_data);

    return sub;
}

int ol_subscription_request(ol_subscription_t *sub, size_t n) {
    if (!sub || sub->unsubscribed) return -1;
    if (n == 0) return 0;
    ol_stream_t *s = sub->parent;

    ol_mutex_lock(&s->mu);
    sub->demand += n;

    /* Deliver buffered items while demand remains */
    while (sub->demand > 0 && s->q_size > 0) {
        void *item = stream_dequeue(s);
        ol_mutex_unlock(&s->mu);
        deliver_to_sub(sub, item);
        if (s->owns_items && s->dtor) s->dtor(item);
        ol_mutex_lock(&s->mu);
    }

    ol_mutex_unlock(&s->mu);
    return 0;
}

int ol_subscription_unsubscribe(ol_subscription_t *sub) {
    if (!sub) return -1;
    sub->unsubscribed = true;
    return 0;
}

void ol_subscription_destroy(ol_subscription_t *sub) {
    if (!sub) return;
    /* Remove from parent list */
    ol_stream_t *s = sub->parent;
    if (s) {
        ol_mutex_lock(&s->mu);
        ol_subscription_t **pp = &s->subs_head;
        while (*pp) {
            if (*pp == sub) { *pp = sub->next; break; }
            pp = &(*pp)->next;
        }
        ol_mutex_unlock(&s->mu);
    }
    free(sub);
}

/* Emission API */

int ol_stream_emit_next(ol_stream_t *s, void *item) {
    if (!s) return -1;

    /* If operator-wired, route through operator handling */
    if (s->on_src_next) {
        s->on_src_next(s, item);
        return 0;
    }

    ol_mutex_lock(&s->mu);
    if (s->state != S_PENDING) {
        ol_mutex_unlock(&s->mu);
        if (s->owns_items && s->dtor) s->dtor(item);
        return -1;
    }

    /* Try direct delivery to subscribers respecting demand; else buffer */
    bool buffered = true;
    for (ol_subscription_t *sub = s->subs_head; sub; sub = sub->next) {
        if (sub->unsubscribed) continue;
        if (sub->demand > 0) {
            sub->demand--;
            buffered = false;
            /* Deliver outside lock */
            ol_on_next_fn fn = sub->on_next;
            void *ud = sub->user_data;
            ol_mutex_unlock(&s->mu);
            if (fn) fn(item, ud);
            ol_mutex_lock(&s->mu);
        }
    }
    if (buffered) {
        stream_enqueue(s, item);
    } else if (s->owns_items && s->dtor) {
        /* item consumed by subscribers; stream owns item and should free after delivery */
        s->dtor(item);
    }

    ol_mutex_unlock(&s->mu);
    return 0;
}

int ol_stream_emit_error(ol_stream_t *s, int code) {
    if (!s) return -1;
    ol_mutex_lock(&s->mu);
    if (s->state != S_PENDING) { ol_mutex_unlock(&s->mu); return -1; }
    s->state = S_ERROR;
    s->error_code = code;
    stream_clear_queue(s);
    ol_mutex_unlock(&s->mu);
    broadcast_error(s, code);
    return 0;
}

int ol_stream_emit_complete(ol_stream_t *s) {
    if (!s) return -1;
    ol_mutex_lock(&s->mu);
    if (s->state != S_PENDING) { ol_mutex_unlock(&s->mu); return -1; }
    s->state = S_COMPLETED;
    stream_clear_queue(s);
    ol_mutex_unlock(&s->mu);
    broadcast_complete(s);
    return 0;
}

/* Operators */

/* Map */
static void op_map_on_next(ol_stream_t *self, const void *item) {
    void *mapped = self->op.map.fn(item, self->op.map.user_data);
    ol_stream_emit_next(self, mapped);
}
static void op_passthrough_error(ol_stream_t *self, int code) { (void)self; (void)code; }
static void op_passthrough_complete(ol_stream_t *self) { (void)self; }

ol_stream_t* ol_stream_map(ol_stream_t *src, ol_map_fn fn, void *user_data, ol_item_destructor out_dtor) {
    if (!src || !fn) return NULL;
    ol_stream_t *s = ol_stream_create(src->loop, out_dtor);
    if (!s) return NULL;

    s->src_a = src;
    s->on_src_next = op_map_on_next;
    s->on_src_error = op_passthrough_error;
    s->on_src_complete = op_passthrough_complete;
    s->op.map.fn = fn;
    s->op.map.user_data = user_data;
    s->op.map.out_dtor = out_dtor;
    s->owns_items = (out_dtor != NULL);
    return s;
}

/* Filter */
static void op_filter_on_next(ol_stream_t *self, const void *item) {
    if (self->op.filter.pred(item, self->op.filter.user_data)) {
        ol_stream_emit_next(self, (void*)item);
    } else {
        /* drop: if we own items, free; else ignore */
        if (self->owns_items && self->dtor) self->dtor((void*)item);
    }
}
ol_stream_t* ol_stream_filter(ol_stream_t *src, ol_filter_fn pred, void *user_data) {
    if (!src || !pred) return NULL;
    /* Filter preserves item ownership of src; do not double free */
    ol_stream_t *s = ol_stream_create(src->loop, src->dtor);
    if (!s) return NULL;
    s->owns_items = src->owns_items;

    s->src_a = src;
    s->on_src_next = op_filter_on_next;
    s->on_src_error = op_passthrough_error;
    s->on_src_complete = op_passthrough_complete;

    s->op.filter.pred = pred;
    s->op.filter.user_data = user_data;
    return s;
}

/* Take N */
static void op_take_on_next(ol_stream_t *self, const void *item) {
    if (self->op.take.remaining == 0) {
        /* Already completed; drop */
        if (self->owns_items && self->dtor) self->dtor((void*)item);
        return;
    }
    self->op.take.remaining--;
    ol_stream_emit_next(self, (void*)item);
    if (self->op.take.remaining == 0) {
        ol_stream_emit_complete(self);
    }
}
ol_stream_t* ol_stream_take(ol_stream_t *src, size_t n) {
    if (!src || n == 0) return NULL;
    ol_stream_t *s = ol_stream_create(src->loop, src->dtor);
    if (!s) return NULL;
    s->owns_items = src->owns_items;

    s->src_a = src;
    s->on_src_next = op_take_on_next;
    s->on_src_error = op_passthrough_error;
    s->on_src_complete = op_passthrough_complete;
    s->op.take.remaining = n;
    return s;
}

/* Merge */
static void op_merge_on_next(ol_stream_t *self, const void *item) {
    ol_stream_emit_next(self, (void*)item);
}
ol_stream_t* ol_stream_merge(ol_stream_t *a, ol_stream_t *b, ol_item_destructor dtor_hint) {
    if (!a || !b) return NULL;
    /* Merge emits items from both; ownership follows hint */
    ol_stream_t *s = ol_stream_create(a->loop, dtor_hint);
    if (!s) return NULL;
    s->owns_items = (dtor_hint != NULL);
    s->src_a = a;
    s->src_b = b;
    s->on_src_next = op_merge_on_next;
    s->on_src_error = op_passthrough_error;
    s->on_src_complete = op_passthrough_complete;
    return s;
}

/* Debounce */
static void op_debounce_on_next(ol_stream_t *self, const void *item) {
    /* Replace last item and (re)schedule timer */
    ol_mutex_lock(&self->mu);
    if (self->op.debounce.have_pending && self->owns_items && self->dtor) {
        self->dtor(self->op.debounce.last_item);
    }
    self->op.debounce.last_item = (void*)item;
    self->op.debounce.have_pending = true;

    /* If no timer, register; otherwise it will fire later */
    if (self->op.debounce.timer_id == 0) {
        ol_deadline_t dl = ol_deadline_from_ns(self->op.debounce.interval_ns);
        self->op.debounce.timer_id = ol_event_loop_register_timer(self->loop, dl, 0, loop_cb_timer, self);
    }
    ol_mutex_unlock(&self->mu);
}
ol_stream_t* ol_stream_debounce(ol_stream_t *src, int64_t interval_ns) {
    if (!src || interval_ns <= 0) return NULL;
    ol_stream_t *s = ol_stream_create(src->loop, src->dtor);
    if (!s) return NULL;
    s->owns_items = src->owns_items;

    s->src_a = src;
    s->on_src_next = op_debounce_on_next;
    s->on_src_error = op_passthrough_error;
    s->on_src_complete = op_passthrough_complete;
    s->op.debounce.interval_ns = interval_ns;
    s->op.debounce.timer_id = 0;
    s->op.debounce.have_pending = false;
    s->op.debounce.last_item = NULL;
    return s;
}

/* Timer source */
ol_stream_t* ol_stream_timer(ol_event_loop_t *loop, int64_t period_ns, size_t count) {
    if (!loop || period_ns <= 0) return NULL;
    ol_stream_t *s = ol_stream_create(loop, NULL);
    if (!s) return NULL;

    /* Register periodic or one-shot timer that emits NULL items */
    ol_deadline_t first = ol_deadline_from_ns(period_ns);
    uint64_t id = ol_event_loop_register_timer(loop, first, (count == 1) ? 0 : period_ns, loop_cb_timer, s);
    (void)id; /* no need to store; stream_emit_complete can be manual by consumer */
    return s;
}

/* IO source */
ol_stream_t* ol_stream_from_fd(ol_event_loop_t *loop, int fd, uint32_t mask) {
    if (!loop || fd < 0) return NULL;
    ol_stream_t *s = ol_stream_create(loop, NULL);
    if (!s) return NULL;

    uint64_t id = ol_event_loop_register_io(loop, fd, mask, loop_cb_io, s);
    if (id == 0) { ol_stream_destroy(s); return NULL; }

    s->op.fd.fd = fd;
    s->op.fd.mask = mask;
    s->op.fd.reg_id = id;
    return s;
}

/* Introspection */

bool ol_stream_is_completed(const ol_stream_t *s) {
    if (!s) return false;
    return s->state == S_COMPLETED || s->state == S_ERROR;
}

size_t ol_stream_subscriber_count(const ol_stream_t *s) {
    if (!s) return 0;
    size_t c = 0;
    const ol_subscription_t *sub = s->subs_head;
    while (sub) { c++; sub = sub->next; }
    return c;
}
