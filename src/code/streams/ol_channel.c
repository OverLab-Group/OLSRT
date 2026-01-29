/**
 * @file ol_channel.c
 * @brief Thread-safe channel implementation for OLSRT
 * @version 1.2.0
 * 
 * This module implements a FIFO channel with optional bounded capacity
 * and item destructor support. Channels are thread-safe and support
 * blocking/non-blocking operations with deadlines.
 */

#include "ol_channel.h"
#include "ol_common.h"
#include "ol_deadlines.h"

#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Internal structures
 * -------------------------------------------------------------------------- */

/**
 * @brief Channel node (linked list)
 */
typedef struct ol_chan_node {
    void *item;                 /**< Item payload */
    struct ol_chan_node *next;  /**< Next node in queue */
} ol_chan_node_t;

/**
 * @brief Channel internal state
 */
struct ol_channel {
    /* Queue state */
    ol_chan_node_t *head;       /**< First node in queue */
    ol_chan_node_t *tail;       /**< Last node in queue */
    size_t size;                /**< Current queue size */
    size_t capacity;            /**< Maximum capacity (0 = unbounded) */
    
    /* Item management */
    ol_chan_item_destructor dtor; /**< Item destructor (optional) */
    
    /* Synchronization */
    ol_mutex_t mutex;           /**< Protects queue state */
    ol_cond_t not_empty;        /**< Signaled when queue becomes non-empty */
    ol_cond_t not_full;         /**< Signaled when queue has space (bounded) */
    
    /* State flags */
    bool closed;                /**< Whether channel is closed */
    bool destroyed;             /**< Whether channel is being destroyed */
};

/* --------------------------------------------------------------------------
 * Internal helper functions
 * -------------------------------------------------------------------------- */

/**
 * @brief Queue push operation (caller must hold mutex)
 */
static void ol_chan_push(ol_channel_t *ch, void *item) {
    ol_chan_node_t *node = (ol_chan_node_t*)malloc(sizeof(ol_chan_node_t));
    if (!node) {
        /* Allocation failure: best-effort cleanup */
        if (ch->dtor) {
            ch->dtor(item);
        }
        return;
    }
    
    node->item = item;
    node->next = NULL;
    
    if (ch->tail) {
        ch->tail->next = node;
        ch->tail = node;
    } else {
        ch->head = ch->tail = node;
    }
    
    ch->size++;
}

/**
 * @brief Queue pop operation (caller must hold mutex)
 */
static int ol_chan_pop(ol_channel_t *ch, void **out_item) {
    if (!ch->head) {
        return 0;
    }
    
    ol_chan_node_t *node = ch->head;
    *out_item = node->item;
    
    ch->head = node->next;
    if (!ch->head) {
        ch->tail = NULL;
    }
    
    free(node);
    ch->size--;
    
    return 1;
}

/**
 * @brief Clear queue and free all items
 */
static void ol_chan_clear(ol_channel_t *ch) {
    while (ch->head) {
        ol_chan_node_t *node = ch->head;
        ch->head = node->next;
        
        if (ch->dtor && node->item) {
            ch->dtor(node->item);
        }
        
        free(node);
    }
    
    ch->tail = NULL;
    ch->size = 0;
}

/**
 * @brief Wait for space in bounded channel
 */
static int ol_chan_wait_for_space(ol_channel_t *ch, int64_t deadline_ns) {
    while (ch->capacity > 0 && ch->size >= ch->capacity && !ch->closed) {
        int r = ol_cond_wait_until(&ch->not_full, &ch->mutex, deadline_ns);
        if (r == 0) {
            return OL_TIMEOUT;
        } else if (r < 0) {
            return OL_ERROR;
        }
        /* Signaled, continue loop */
    }
    
    return OL_SUCCESS;
}

/**
 * @brief Wait for item in channel
 */
static int ol_chan_wait_for_item(ol_channel_t *ch, int64_t deadline_ns) {
    while (ch->size == 0 && !ch->closed) {
        int r = ol_cond_wait_until(&ch->not_empty, &ch->mutex, deadline_ns);
        if (r == 0) {
            return OL_TIMEOUT;
        } else if (r < 0) {
            return OL_ERROR;
        }
        /* Signaled, continue loop */
    }
    
    return OL_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Public API implementation
 * -------------------------------------------------------------------------- */

ol_channel_t* ol_channel_create(size_t capacity,
                                ol_chan_item_destructor dtor) {
    ol_channel_t *ch = (ol_channel_t*)calloc(1, sizeof(ol_channel_t));
    if (!ch) {
        return NULL;
    }
    
    ch->head = NULL;
    ch->tail = NULL;
    ch->size = 0;
    ch->capacity = capacity;
    ch->dtor = dtor;
    ch->closed = false;
    ch->destroyed = false;
    
    /* Initialize synchronization primitives */
    if (ol_mutex_init(&ch->mutex) != OL_SUCCESS) {
        free(ch);
        return NULL;
    }
    
    if (ol_cond_init(&ch->not_empty) != OL_SUCCESS) {
        ol_mutex_destroy(&ch->mutex);
        free(ch);
        return NULL;
    }
    
    if (ol_cond_init(&ch->not_full) != OL_SUCCESS) {
        ol_cond_destroy(&ch->not_empty);
        ol_mutex_destroy(&ch->mutex);
        free(ch);
        return NULL;
    }
    
    return ch;
}

void ol_channel_destroy(ol_channel_t *ch) {
    if (!ch) {
        return;
    }
    
    ol_mutex_lock(&ch->mutex);
    
    if (ch->destroyed) {
        ol_mutex_unlock(&ch->mutex);
        return; /* Already being destroyed */
    }
    
    ch->destroyed = true;
    ch->closed = true;
    
    /* Wake all waiters */
    ol_cond_broadcast(&ch->not_empty);
    ol_cond_broadcast(&ch->not_full);
    
    /* Clear queue */
    ol_chan_clear(ch);
    
    ol_mutex_unlock(&ch->mutex);
    
    /* Destroy synchronization primitives */
    ol_cond_destroy(&ch->not_full);
    ol_cond_destroy(&ch->not_empty);
    ol_mutex_destroy(&ch->mutex);
    
    /* Free channel structure */
    free(ch);
}

int ol_channel_close(ol_channel_t *ch) {
    if (!ch) {
        return OL_ERROR;
    }
    
    ol_mutex_lock(&ch->mutex);
    
    if (ch->closed) {
        ol_mutex_unlock(&ch->mutex);
        return OL_SUCCESS; /* Already closed */
    }
    
    ch->closed = true;
    
    /* Wake all waiters */
    ol_cond_broadcast(&ch->not_empty);
    ol_cond_broadcast(&ch->not_full);
    
    ol_mutex_unlock(&ch->mutex);
    
    return OL_SUCCESS;
}

int ol_channel_send(ol_channel_t *ch, void *item) {
    return ol_channel_send_deadline(ch, item, 0);
}

int ol_channel_send_deadline(ol_channel_t *ch,
                             void *item,
                             int64_t deadline_ns) {
    if (!ch) {
        if (item) {
            /* Caller still owns item */
        }
        return OL_ERROR;
    }
    
    ol_mutex_lock(&ch->mutex);
    
    if (ch->closed) {
        ol_mutex_unlock(&ch->mutex);
        /* Channel closed: destroy item */
        if (item && ch->dtor) {
            ch->dtor(item);
        }
        return OL_CLOSED;
    }
    
    /* Wait for space if bounded */
    if (ch->capacity > 0) {
        int r = ol_chan_wait_for_space(ch, deadline_ns);
        if (r != OL_SUCCESS) {
            ol_mutex_unlock(&ch->mutex);
            /* Wait failed: caller still owns item */
            return r;
        }
    }
    
    /* Check if closed while waiting */
    if (ch->closed) {
        ol_mutex_unlock(&ch->mutex);
        if (item && ch->dtor) {
            ch->dtor(item);
        }
        return OL_CLOSED;
    }
    
    /* Enqueue item */
    ol_chan_push(ch, item);
    
    /* Signal receivers */
    ol_cond_signal(&ch->not_empty);
    
    ol_mutex_unlock(&ch->mutex);
    
    return OL_SUCCESS;
}

int ol_channel_try_send(ol_channel_t *ch, void *item) {
    if (!ch) {
        return OL_ERROR;
    }
    
    ol_mutex_lock(&ch->mutex);
    
    if (ch->closed) {
        ol_mutex_unlock(&ch->mutex);
        if (item && ch->dtor) {
            ch->dtor(item);
        }
        return OL_CLOSED;
    }
    
    /* Check if bounded and full */
    if (ch->capacity > 0 && ch->size >= ch->capacity) {
        ol_mutex_unlock(&ch->mutex);
        return 0; /* Would block */
    }
    
    /* Enqueue item */
    ol_chan_push(ch, item);
    
    /* Signal receivers */
    ol_cond_signal(&ch->not_empty);
    
    ol_mutex_unlock(&ch->mutex);
    
    return 1;
}

int ol_channel_recv(ol_channel_t *ch, void **out_item) {
    return ol_channel_recv_deadline(ch, out_item, 0);
}

int ol_channel_recv_deadline(ol_channel_t *ch,
                             void **out_item,
                             int64_t deadline_ns) {
    if (!ch || !out_item) {
        return OL_ERROR;
    }
    
    *out_item = NULL;
    
    ol_mutex_lock(&ch->mutex);
    
    /* Wait for item */
    int r = ol_chan_wait_for_item(ch, deadline_ns);
    if (r != OL_SUCCESS) {
        ol_mutex_unlock(&ch->mutex);
        return r;
    }
    
    /* Check state after wait */
    if (ch->size == 0 && ch->closed) {
        /* Channel closed and empty */
        ol_mutex_unlock(&ch->mutex);
        *out_item = NULL;
        return 0;
    }
    
    /* Dequeue item */
    void *item = NULL;
    ol_chan_pop(ch, &item);
    *out_item = item;
    
    /* Signal senders if bounded */
    if (ch->capacity > 0) {
        ol_cond_signal(&ch->not_full);
    }
    
    ol_mutex_unlock(&ch->mutex);
    
    return 1;
}

int ol_channel_try_recv(ol_channel_t *ch, void **out_item) {
    if (!ch || !out_item) {
        return OL_ERROR;
    }
    
    *out_item = NULL;
    
    ol_mutex_lock(&ch->mutex);
    
    if (ch->size == 0) {
        ol_mutex_unlock(&ch->mutex);
        return 0; /* Would block */
    }
    
    /* Dequeue item */
    void *item = NULL;
    ol_chan_pop(ch, &item);
    *out_item = item;
    
    /* Signal senders if bounded */
    if (ch->capacity > 0) {
        ol_cond_signal(&ch->not_full);
    }
    
    ol_mutex_unlock(&ch->mutex);
    
    return 1;
}

bool ol_channel_is_closed(const ol_channel_t *ch) {
    if (!ch) {
        return true;
    }
    
    bool closed;
    
    ol_mutex_lock((ol_mutex_t*)&ch->mutex);
    closed = ch->closed;
    ol_mutex_unlock((ol_mutex_t*)&ch->mutex);
    
    return closed;
}

size_t ol_channel_len(const ol_channel_t *ch) {
    if (!ch) {
        return 0;
    }
    
    size_t len;
    
    ol_mutex_lock((ol_mutex_t*)&ch->mutex);
    len = ch->size;
    ol_mutex_unlock((ol_mutex_t*)&ch->mutex);
    
    return len;
}

size_t ol_channel_capacity(const ol_channel_t *ch) {
    return ch ? ch->capacity : 0;
}