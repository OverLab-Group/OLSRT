/**
 * @file ol_channel.c
 * @brief Thread-safe channel (queue) implementation with optional bounded capacity and item destructor.
 *
 * Overview
 * --------
 * This module implements a simple FIFO channel with optional capacity (0 = unbounded).
 * It supports blocking send/recv with deadlines, try_send/try_recv non-blocking variants,
 * and close semantics. The channel owns an optional item destructor which is authoritative
 * for cleaning up queued items on channel destruction or when send fails due to closed channel.
 *
 * Return codes
 * ------------
 * - For send/recv functions we use:
 *   *  1 : success (for try_send/try_recv when item returned immediately)
 *   *  0 : closed+empty (recv) or would-block (try_send/try_recv) or success for blocking send
 *   * -2 : closed (send when channel closed)
 *   * -3 : timeout (deadline expired)
 *   * -1 : generic error (invalid args or internal error)
 *
 * Thread-safety
 * -------------
 * - All public APIs are thread-safe. Internal synchronization uses ol_mutex_t and ol_cond_t.
 * - The channel's destructor (dtor) must be safe to call from any thread context.
 *
 * Ownership model
 * ---------------
 * - When sending an item, the caller transfers ownership to the channel.
 * - If send fails due to closed channel, the channel will call the destructor (if set) to free the item.
 * - On channel_destroy, all queued items are freed via the channel's destructor.
 *
 * Testing and tooling
 * -------------------
 * - Unit tests should cover bounded/unbounded behavior, deadline semantics, close semantics,
 *   and destructor invocation paths.
 * - Use ASan/Valgrind to detect leaks; TSan to detect races in concurrent send/recv scenarios.
 * - Fuzz harnesses (AFL++/libFuzzer) can exercise edge cases: rapid close/send races, spurious wakeups.
 * - Static analyzers (clang-tidy, cppcheck) will benefit from explicit null checks and documented return codes.
 */

#include "ol_channel.h"

#include <stdlib.h>
#include <string.h>

/* Queue node */
typedef struct ol_chan_node {
    void *item;
    struct ol_chan_node *next;
} ol_chan_node_t;

/**
 * @struct ol_channel
 * @brief Internal channel representation.
 *
 * Fields:
 * - head/tail: linked list queue nodes
 * - size: current number of items
 * - capacity: 0 => unbounded; otherwise maximum queued items
 * - dtor: destructor for queued items (may be NULL)
 * - mu: mutex protecting queue and state
 * - cv_not_empty: condition variable signaled when queue becomes non-empty
 * - cv_not_full: condition variable signaled when queue has space (bounded)
 * - closed: boolean indicating channel closed (no further sends accepted)
 */
struct ol_channel {
    ol_chan_node_t *head;
    ol_chan_node_t *tail;
    size_t          size;
    size_t          capacity;  /* 0 => unbounded */

    ol_chan_item_destructor dtor;

    ol_mutex_t mu;
    ol_cond_t  cv_not_empty;
    ol_cond_t  cv_not_full;

    bool closed;
};

/* -------------------- Internal queue helpers -------------------- */

/**
 * @brief Push an item to the queue tail. Caller must hold channel mutex.
 *
 * @param ch Channel pointer (non-NULL)
 * @param item Item pointer (ownership transferred)
 */
static void q_push(ol_channel_t *ch, void *item) {
    ol_chan_node_t *n = (ol_chan_node_t*)malloc(sizeof(ol_chan_node_t));
    if (!n) return; /* allocation failure: best-effort (caller cannot handle here) */
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

/**
 * @brief Pop one item from queue head. Caller must hold channel mutex.
 *
 * @param ch Channel pointer (non-NULL)
 * @param out_item Out parameter for popped item (may be NULL if queue empty)
 * @return int 1 if item popped, 0 if queue empty
 */
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

/**
 * @brief Clear queue and free nodes. Uses channel destructor for items if set.
 *
 * Caller must hold channel mutex.
 *
 * @param ch Channel pointer (non-NULL)
 */
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

/* -------------------- Public API implementations -------------------- */

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

/**
 * @brief Destroy channel: close, wake waiters, clear queue and free resources.
 *
 * Note: This function will call the channel destructor for queued items.
 *
 * @param ch Channel pointer (may be NULL)
 */
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

/**
 * @brief Close channel: subsequent sends fail; receivers can drain remaining items.
 *
 * @param ch Channel pointer
 * @return int 0 on success, -1 on invalid arg
 */
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

/**
 * @brief Blocking send (infinite wait) implemented via send_deadline with deadline=0.
 *
 * @param ch Channel pointer
 * @param item Item pointer (ownership transferred)
 * @return int 0 on success, -1 error, -2 closed, -3 timeout (not used here)
 */
int ol_channel_send(ol_channel_t *ch, void *item) {
    if (!ch) return -1;
    return ol_channel_send_deadline(ch, item, /*deadline*/ 0);
}

/**
 * @brief Send with absolute deadline (nanoseconds). Blocks until space available or deadline.
 *
 * Returns:
 * - 0 on success
 * - -2 if channel closed (item destroyed via dtor if set)
 * - -3 on timeout
 * - -1 on error
 *
 * @param ch Channel pointer
 * @param item Item pointer (ownership transferred)
 * @param deadline_ns Absolute deadline in ns (0 => infinite wait)
 * @return int status code
 */
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

/**
 * @brief Try to send without blocking.
 *
 * Returns:
 * - 1 if enqueued
 * - 0 if would-block (bounded and full)
 * - -2 if closed (item destroyed via dtor)
 * - -1 on invalid arg
 *
 * @param ch Channel pointer
 * @param item Item pointer (ownership transferred on success; on closed it is destroyed)
 * @return int status code
 */
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

/**
 * @brief Blocking receive (infinite wait) implemented via recv_deadline with deadline=0.
 *
 * @param ch Channel pointer
 * @param out Out parameter for item (set to NULL on closed/empty)
 * @return int 1 item received; 0 closed+empty; -3 timeout; -1 error
 */
int ol_channel_recv(ol_channel_t *ch, void **out) {
    if (!ch || !out) return -1;
    return ol_channel_recv_deadline(ch, out, /*deadline*/ 0);
}

/**
 * @brief Receive with absolute deadline (nanoseconds).
 *
 * Returns:
 * - 1 if item received (out set)
 * - 0 if channel closed and empty (out set to NULL)
 * - -3 on timeout (out set to NULL)
 * - -1 on error
 *
 * @param ch Channel pointer
 * @param out Out parameter for item
 * @param deadline_ns Absolute deadline in ns (0 => infinite wait)
 * @return int status code
 */
int ol_channel_recv_deadline(ol_channel_t *ch, void **out, int64_t deadline_ns) {
    if (!ch || !out) return -1;

    ol_mutex_lock(&ch->mu);

    /* Wait until item available or closed+empty */
    while (ch->size == 0 && !ch->closed) {
        int r = ol_cond_wait_until(&ch->cv_not_empty, &ch->mu, deadline_ns);
        if (r == 0) { /* timeout */
            ol_mutex_unlock(&ch->mu);
            *out = NULL;
            return -3;
        }
        if (r < 0) { /* error */
            ol_mutex_unlock(&ch->mu);
            *out = NULL;
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

/**
 * @brief Try to receive without blocking.
 *
 * Returns:
 * - 1 if item received (out set)
 * - 0 if would-block or closed+empty (out set to NULL)
 * - -1 on error
 *
 * Note: try_recv treats closed+empty as would-block (0) for symmetry with try_send.
 *
 * @param ch Channel pointer
 * @param out Out parameter for item
 * @return int status code
 */
int ol_channel_try_recv(ol_channel_t *ch, void **out) {
    if (!ch || !out) return -1;

    ol_mutex_lock(&ch->mu);

    if (ch->size == 0) {
        bool closed_empty = ch->closed;
        ol_mutex_unlock(&ch->mu);
        *out = NULL;
        return closed_empty ? 0 /* closed+empty treated as would-block */ : 0;
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

/* -------------------- Introspection -------------------- */

/**
 * @brief Check whether channel is closed.
 *
 * @param ch Channel pointer
 * @return bool true if closed, false otherwise
 */
bool ol_channel_is_closed(const ol_channel_t *ch) {
    if (!ch) return false;
    bool c;
    ol_mutex_lock((ol_mutex_t*)&ch->mu);
    c = ch->closed;
    ol_mutex_unlock((ol_mutex_t*)&ch->mu);
    return c;
}

/**
 * @brief Get current queue length.
 *
 * @param ch Channel pointer
 * @return size_t number of queued items or 0 on invalid arg
 */
size_t ol_channel_len(const ol_channel_t *ch) {
    if (!ch) return 0;
    size_t len;
    ol_mutex_lock((ol_mutex_t*)&ch->mu);
    len = ch->size;
    ol_mutex_unlock((ol_mutex_t*)&ch->mu);
    return len;
}

/**
 * @brief Get channel capacity (0 = unbounded).
 *
 * @param ch Channel pointer
 * @return size_t capacity or 0 on invalid arg
 */
size_t ol_channel_capacity(const ol_channel_t *ch) {
    return ch ? ch->capacity : 0;
}