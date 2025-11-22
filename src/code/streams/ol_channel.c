#include "ol_channel.h"

#include <stdlib.h>
#include <string.h>

/* Queue node */
typedef struct ol_chan_node {
    void *item;
    struct ol_chan_node *next;
} ol_chan_node_t;

struct ol_channel {
    /* Queue */
    ol_chan_node_t *head;
    ol_chan_node_t *tail;
    size_t          size;
    size_t          capacity;  /* 0 => unbounded */

    /* Ownership */
    ol_chan_item_destructor dtor;

    /* Sync */
    ol_mutex_t mu;
    ol_cond_t  cv_not_empty;
    ol_cond_t  cv_not_full;

    /* State */
    bool closed;
};

/* Internal helpers */

static void q_push(ol_channel_t *ch, void *item) {
    ol_chan_node_t *n = (ol_chan_node_t*)malloc(sizeof(ol_chan_node_t));
    n->item = item;
    n->next = NULL;
    if (!ch->tail) {
        ch->head = ch->tail = n;
    } else {
        ch->tail->next = n;
        ch->tail = n;
    }
    ch->size++;
}

static int q_pop(ol_channel_t *ch, void **out_item) {
    ol_chan_node_t *n = ch->head;
    if (!n) return 0;
    ch->head = n->next;
    if (!ch->head) ch->tail = NULL;
    ch->size--;
    *out_item = n->item;
    free(n);
    return 1;
}

static void q_clear(ol_channel_t *ch) {
    while (ch->head) {
        ol_chan_node_t *n = ch->head;
        ch->head = n->next;
        if (ch->dtor) ch->dtor(n->item);
        free(n);
    }
    ch->tail = NULL;
    ch->size = 0;
}

/* API implementations */

ol_channel_t* ol_channel_create(size_t capacity, ol_chan_item_destructor dtor) {
    ol_channel_t *ch = (ol_channel_t*)calloc(1, sizeof(ol_channel_t));
    if (!ch) return NULL;
    ch->head = ch->tail = NULL;
    ch->size = 0;
    ch->capacity = capacity; /* 0 = unbounded */
    ch->dtor = dtor;
    ch->closed = false;

    if (ol_mutex_init(&ch->mu) != 0) { free(ch); return NULL; }
    if (ol_cond_init(&ch->cv_not_empty) != 0) { ol_mutex_destroy(&ch->mu); free(ch); return NULL; }
    if (ol_cond_init(&ch->cv_not_full) != 0) {
        ol_cond_destroy(&ch->cv_not_empty);
        ol_mutex_destroy(&ch->mu);
        free(ch);
        return NULL;
    }

    return ch;
}

void ol_channel_destroy(ol_channel_t *ch) {
    if (!ch) return;
    ol_mutex_lock(&ch->mu);
    ch->closed = true;
    ol_cond_broadcast(&ch->cv_not_empty);
    ol_cond_broadcast(&ch->cv_not_full);
    q_clear(ch);
    ol_mutex_unlock(&ch->mu);

    ol_cond_destroy(&ch->cv_not_full);
    ol_cond_destroy(&ch->cv_not_empty);
    ol_mutex_destroy(&ch->mu);
    free(ch);
}

int ol_channel_close(ol_channel_t *ch) {
    if (!ch) return -1;
    ol_mutex_lock(&ch->mu);
    if (ch->closed) {
        ol_mutex_unlock(&ch->mu);
        return 0;
    }
    ch->closed = true;
    /* Wake all waiters: receivers to drain, senders to fail */
    ol_cond_broadcast(&ch->cv_not_empty);
    ol_cond_broadcast(&ch->cv_not_full);
    ol_mutex_unlock(&ch->mu);
    return 0;
}

int ol_channel_send(ol_channel_t *ch, void *item) {
    if (!ch) return -1;
    /* Infinite wait */
    return ol_channel_send_deadline(ch, item, /*deadline*/ 0);
}

int ol_channel_send_deadline(ol_channel_t *ch, void *item, int64_t deadline_ns) {
    if (!ch) return -1;

    ol_mutex_lock(&ch->mu);

    if (ch->closed) {
        ol_mutex_unlock(&ch->mu);
        if (ch->dtor) ch->dtor(item);
        return -2; /* closed */
    }

    /* Wait until space available or deadline */
    while (ch->capacity > 0 && ch->size >= ch->capacity && !ch->closed) {
        int r = ol_cond_wait_until(&ch->cv_not_full, &ch->mu, deadline_ns);
        if (r == 0) { /* timeout */
            ol_mutex_unlock(&ch->mu);
            return -3;
        }
        if (r < 0) { /* error */
            ol_mutex_unlock(&ch->mu);
            return -1;
        }
        /* else signaled; loop to re-check conditions */
    }

    if (ch->closed) {
        ol_mutex_unlock(&ch->mu);
        if (ch->dtor) ch->dtor(item);
        return -2;
    }

    q_push(ch, item);
    /* Wake one receiver */
    ol_cond_signal(&ch->cv_not_empty);
    ol_mutex_unlock(&ch->mu);
    return 0;
}

int ol_channel_try_send(ol_channel_t *ch, void *item) {
    if (!ch) return -1;

    ol_mutex_lock(&ch->mu);

    if (ch->closed) {
        ol_mutex_unlock(&ch->mu);
        if (ch->dtor) ch->dtor(item);
        return -2;
    }

    if (ch->capacity > 0 && ch->size >= ch->capacity) {
        ol_mutex_unlock(&ch->mu);
        return 0; /* would-block */
    }

    q_push(ch, item);
    ol_cond_signal(&ch->cv_not_empty);
    ol_mutex_unlock(&ch->mu);
    return 1;
}

int ol_channel_recv(ol_channel_t *ch, void **out) {
    if (!ch || !out) return -1;
    return ol_channel_recv_deadline(ch, out, /*deadline*/ 0);
}

int ol_channel_recv_deadline(ol_channel_t *ch, void **out, int64_t deadline_ns) {
    if (!ch || !out) return -1;

    ol_mutex_lock(&ch->mu);

    /* Wait until item available or closed+empty */
    while (ch->size == 0 && !ch->closed) {
        int r = ol_cond_wait_until(&ch->cv_not_empty, &ch->mu, deadline_ns);
        if (r == 0) { /* timeout */
            ol_mutex_unlock(&ch->mu);
            return -3;
        }
        if (r < 0) { /* error */
            ol_mutex_unlock(&ch->mu);
            return -1;
        }
    }

    if (ch->size == 0 && ch->closed) {
        ol_mutex_unlock(&ch->mu);
        *out = NULL;
        return 0; /* closed + empty */
    }

    /* Pop one item */
    void *item = NULL;
    (void)q_pop(ch, &item);

    /* Wake one sender (bounded queues) */
    if (ch->capacity > 0) {
        ol_cond_signal(&ch->cv_not_full);
    }

    ol_mutex_unlock(&ch->mu);
    *out = item;
    return 1;
}

int ol_channel_try_recv(ol_channel_t *ch, void **out) {
    if (!ch || !out) return -1;

    ol_mutex_lock(&ch->mu);

    if (ch->size == 0) {
        bool closed_empty = ch->closed;
        ol_mutex_unlock(&ch->mu);
        *out = NULL;
        return closed_empty ? 0 /* closed+empty, but try_recv treats as would-block */ : 0;
    }

    void *item = NULL;
    (void)q_pop(ch, &item);

    if (ch->capacity > 0) {
        ol_cond_signal(&ch->cv_not_full);
    }

    ol_mutex_unlock(&ch->mu);
    *out = item;
    return 1;
}

/* Introspection */

bool ol_channel_is_closed(const ol_channel_t *ch) {
    if (!ch) return false;
    bool c;
    ol_mutex_lock((ol_mutex_t*)&ch->mu);
    c = ch->closed;
    ol_mutex_unlock((ol_mutex_t*)&ch->mu);
    return c;
}

size_t ol_channel_len(const ol_channel_t *ch) {
    if (!ch) return 0;
    size_t len;
    ol_mutex_lock((ol_mutex_t*)&ch->mu);
    len = ch->size;
    ol_mutex_unlock((ol_mutex_t*)&ch->mu);
    return len;
}

size_t ol_channel_capacity(const ol_channel_t *ch) {
    return ch ? ch->capacity : 0;
}
