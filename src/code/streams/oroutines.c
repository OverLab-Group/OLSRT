/**
 * @file oroutines.c
 * @brief Goroutine-like cooperative oroutines and channels.
 *
 * v1.3.4.1. Built on the cooperative scheduler in
 * ol_green_threads.c (ol_gt_scheduler_run/block/wake).
 *
 * Channel semantics:
 *   - Bounded (capacity > 0) or unbuffered (capacity == 0).
 *   - Sender blocks when there is no room and no waiting receiver.
 *   - Receiver blocks when there is no data and no waiting sender.
 *   - Element data is copied into a heap buffer on the sender side
 *     before the sender blocks. This is required: the caller's
 *     `elem` pointer points at a local variable on the sender's
 *     stack, which is not valid after the sender yields and the
 *     stack frame is reused. The receiver frees the buffer after
 *     the memcpy.
 *
 * Single-threaded: all calls must be on the same OS thread.
 */

#include "oroutines.h"
#include "ol_green_threads.h"
#include "ol_common.h"

#include <stdlib.h>
#include <string.h>

/* ==================== Types ==================== */

struct oroutine {
    ol_gt_t* gt;
    or_fn fn;
    void* arg;

    /* channel handoff state (single-thread; no locks) */
    struct or_chan* wait_chan;
    void* wait_buf;      /* recv: output buffer (caller's); send: heap copy */
    int   wait_result;   /* 0 success, -1 closed-send, 1 closed-recv */
};

struct or_chan {
    size_t   elem_size;
    size_t   capacity;
    char*    buffer;
    size_t   head, tail, len;
    int      closed;

    oroutine_t** senders;
    size_t       n_senders;
    size_t       cap_senders;

    oroutine_t** receivers;
    size_t       n_receivers;
    size_t       cap_receivers;

    uint64_t total_sent;
    uint64_t total_recv;
};

/* ==================== Helpers ==================== */

static oroutine_t* or_current(void) {
    ol_gt_t* gt = ol_gt_current();
    return gt ? (oroutine_t*)gt->arg : NULL;
}

static void waitq_push(oroutine_t*** q, size_t* n, size_t* cap, oroutine_t* r) {
    if (*n >= *cap) {
        size_t newcap = *cap ? *cap * 2 : 8;
        oroutine_t** nq = realloc(*q, newcap * sizeof(oroutine_t*));
        if (!nq) return;
        *q = nq;
        *cap = newcap;
    }
    (*q)[(*n)++] = r;
}

static oroutine_t* waitq_pop(oroutine_t** q, size_t* n) {
    if (*n == 0) return NULL;
    oroutine_t* r = q[0];
    memmove(&q[0], &q[1], (*n - 1) * sizeof(oroutine_t*));
    (*n)--;
    return r;
}

static void or_trampoline(void* arg) {
    oroutine_t* r = (oroutine_t*)arg;
    r->fn(r->arg);
    /* arg already holds r; or_current() reads it. */
}

/* ==================== Lifecycle ==================== */

oroutine_t* or_go(or_fn fn, void* arg) {
    if (!fn) return NULL;
    oroutine_t* r = calloc(1, sizeof(oroutine_t));
    if (!r) return NULL;
    r->fn = fn;
    r->arg = arg;
    r->gt = ol_gt_spawn(or_trampoline, r, 0);
    if (!r->gt) { free(r); return NULL; }
    /* ol_gt_spawn creates a LAZY thread. ol_gt_resume materializes
     * the stack/context, sets READY, and enqueues it. */
    if (ol_gt_resume(r->gt) != 0) {
        ol_gt_destroy(r->gt);
        free(r);
        return NULL;
    }
    return r;
}

void or_yield(void) {
    ol_gt_yield();
}

oroutine_t* or_self(void) {
    return or_current();
}

int or_join(oroutine_t* r, int64_t deadline_ns) {
    if (!r) return -1;
    (void)deadline_ns;
    return ol_gt_scheduler_run() >= 0 ? 0 : -1;
}

void or_cancel(oroutine_t* r) {
    if (!r || !r->gt) return;
    ol_gt_cancel(r->gt);
}

int or_run(void) {
    int total = 0;
    while (1) {
        int n = ol_gt_scheduler_run();
        if (n < 0) return -1;
        total += n;
        if (n == 0) break;
    }
    return total;
}

/* ==================== Channels ==================== */

or_chan_t* or_chan_create(size_t elem_size, size_t capacity) {
    if (elem_size == 0 || elem_size > 65536) return NULL;
    or_chan_t* c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->elem_size = elem_size;
    c->capacity  = capacity;
    c->buffer    = malloc(elem_size * (capacity ? capacity : 1));
    if (!c->buffer) { free(c); return NULL; }
    return c;
}

/* Try to hand the item directly to a waiting receiver.
 * The receiver's wait_buf is the caller's `out` pointer, which is
 * still valid (the receiver has not been woken yet, so its stack
 * frame is intact). */
static int try_handoff_to_receiver(or_chan_t* c, const void* elem) {
    oroutine_t* recv_r = waitq_pop(c->receivers, &c->n_receivers);
    if (!recv_r) return 0;
    if (!recv_r->wait_buf) {
        /* Should not happen — receiver only queues after setting
         * wait_buf. Guard for defensive robustness. */
        return 0;
    }
    memcpy(recv_r->wait_buf, elem, c->elem_size);
    recv_r->wait_result = 0;
    ol_gt_wake(recv_r->gt);
    return 1;
}

int or_chan_send(or_chan_t* c, const void* elem) {
    if (!c || !elem) return -1;
    if (c->closed) return -1;

    /* Direct handoff to a waiting receiver. */
    if (try_handoff_to_receiver(c, elem)) {
        c->total_sent++;
        return 0;
    }

    /* Buffer available? */
    if (c->len < c->capacity) {
        memcpy(c->buffer + c->tail * c->elem_size, elem, c->elem_size);
        c->tail = (c->tail + 1) % c->capacity;
        c->len++;
        c->total_sent++;
        /* Wake a blocked receiver if any (they queued before the
         * buffer had data). */
        oroutine_t* recv_r = waitq_pop(c->receivers, &c->n_receivers);
        if (recv_r && recv_r->wait_buf) {
            memcpy(recv_r->wait_buf,
                   c->buffer + c->head * c->elem_size, c->elem_size);
            c->head = (c->head + 1) % c->capacity;
            c->len--;
            recv_r->wait_result = 0;
            ol_gt_wake(recv_r->gt);
            c->total_recv++;
        }
        return 0;
    }

    /* Block sender. Copy the data to a heap buffer FIRST — the caller's
     * `elem` points at a local variable on the sender's stack, which
     * is invalid after the sender yields and the stack is reused. */
    oroutine_t* self = or_current();
    if (!self || !self->gt) return -1;

    void* copy = malloc(c->elem_size);
    if (!copy) return -1;
    memcpy(copy, elem, c->elem_size);

    self->wait_chan   = c;
    self->wait_buf    = copy;
    self->wait_result = 0;
    waitq_push(&c->senders, &c->n_senders, &c->cap_senders, self);

    ol_gt_block(self->gt);

    /* Woken: receiver copied from copy and freed it, OR channel closed
     * (or_chan_close frees it). Either way, self->wait_buf is NULL now. */
    if (self->wait_result == 0) c->total_sent++;
    self->wait_chan = NULL;
    self->wait_buf = NULL;
    return self->wait_result;
}

int or_chan_recv(or_chan_t* c, void* out) {
    if (!c || !out) return -1;

    /* Buffered data? */
    if (c->len > 0) {
        memcpy(out, c->buffer + c->head * c->elem_size, c->elem_size);
        c->head = (c->head + 1) % c->capacity;
        c->len--;
        c->total_recv++;
        /* There is now a free slot. Wake a blocked sender, whose data
         * was already copied to a heap buffer, and move that data into
         * the buffer. */
        oroutine_t* s = waitq_pop(c->senders, &c->n_senders);
        if (s && s->wait_buf) {
            memcpy(c->buffer + c->tail * c->elem_size,
                   s->wait_buf, c->elem_size);
            free(s->wait_buf);
            s->wait_buf = NULL;
            c->tail = (c->tail + 1) % c->capacity;
            c->len++;
            s->wait_result = 0;
            ol_gt_wake(s->gt);
        }
        return 0;
    }

    /* Empty. Direct handoff from a blocked sender? */
    if (c->n_senders > 0) {
        oroutine_t* s = waitq_pop(c->senders, &c->n_senders);
        if (s && s->wait_buf) {
            memcpy(out, s->wait_buf, c->elem_size);
            free(s->wait_buf);
            s->wait_buf = NULL;
            s->wait_result = 0;
            ol_gt_wake(s->gt);
            c->total_recv++;
            return 0;
        }
    }

    if (c->closed) return 1;

    /* Block receiver. `out` is the caller's buffer; it stays valid
     * because the caller's stack frame is preserved across the yield
     * (the sender wakes us with a memcpy into it, then we return
     * before the caller reuses the stack). */
    oroutine_t* self = or_current();
    if (!self || !self->gt) return -1;

    self->wait_chan   = c;
    self->wait_buf    = out;
    self->wait_result = 1;
    waitq_push(&c->receivers, &c->n_receivers, &c->cap_receivers, self);

    ol_gt_block(self->gt);

    if (self->wait_result == 0) c->total_recv++;
    self->wait_chan = NULL;
    self->wait_buf = NULL;
    return self->wait_result;
}

int or_chan_close(or_chan_t* c) {
    if (!c || c->closed) return -1;
    c->closed = 1;

    oroutine_t* s;
    while ((s = waitq_pop(c->senders, &c->n_senders)) != NULL) {
        if (s->wait_buf) {
            free(s->wait_buf);
            s->wait_buf = NULL;
        }
        s->wait_result = -1;
        ol_gt_wake(s->gt);
    }

    oroutine_t* r;
    while ((r = waitq_pop(c->receivers, &c->n_receivers)) != NULL) {
        r->wait_buf = NULL;
        r->wait_result = 1;
        ol_gt_wake(r->gt);
    }
    return 0;
}

void or_chan_destroy(or_chan_t* c) {
    if (!c) return;
    free(c->buffer);
    free(c->senders);
    free(c->receivers);
    free(c);
}

size_t or_chan_len(const or_chan_t* c) {
    return c ? c->len : 0;
}

size_t or_chan_cap(const or_chan_t* c) {
    return c ? c->capacity : 0;
}

int or_chan_is_closed(const or_chan_t* c) {
    return c ? c->closed : 0;
}
