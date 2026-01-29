/**
 * @file ol_promise.c
 * @brief Promise/Future implementation with thread-safe resolution
 * @version 1.2.0
 * 
 * This module implements a promise/future abstraction with support for
 * continuations, cancellation, and thread-safe resolution.
 */

#include "ol_promise.h"
#include "ol_common.h"
#include "ol_lock_mutex.h"
#include "ol_deadlines.h"

#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Internal data structures
 * -------------------------------------------------------------------------- */

/**
 * @brief Continuation list node
 */
typedef struct ol_cont_node {
    ol_future_cb cb;                /**< Callback function */
    void *user_data;                /**< User data for callback */
    struct ol_cont_node *next;      /**< Next node in list */
} ol_cont_node_t;

/**
 * @brief Shared promise/future core
 */
typedef struct ol_core {
    ol_mutex_t mu;                  /**< Mutex for thread safety */
    ol_cond_t cv;                   /**< Condition variable for waiting */
    
    ol_promise_state_t state;       /**< Current promise state */
    void *value;                    /**< Fulfilled value (owned) */
    ol_value_destructor dtor;       /**< Value destructor */
    int error_code;                 /**< Rejection error code */
    
    bool value_taken;               /**< Whether value was taken by future */
    
    ol_cont_node_t *conts;          /**< List of continuations */
    
    int ref_count;                  /**< Reference count (promise + futures) */
    
    ol_event_loop_t *loop;          /**< Event loop for waking (optional) */
} ol_core_t;

/**
 * @brief Promise structure
 */
struct ol_promise {
    ol_core_t *core;                /**< Shared core */
};

/**
 * @brief Future structure
 */
struct ol_future {
    ol_core_t *core;                /**< Shared core */
};

/* --------------------------------------------------------------------------
 * Core management functions
 * -------------------------------------------------------------------------- */

/**
 * @brief Create a new shared core
 */
static ol_core_t* ol_core_create(ol_event_loop_t *loop) {
    ol_core_t *c = (ol_core_t*)calloc(1, sizeof(ol_core_t));
    if (!c) {
        return NULL;
    }
    
    if (ol_mutex_init(&c->mu) != OL_SUCCESS) {
        free(c);
        return NULL;
    }
    
    if (ol_cond_init(&c->cv) != OL_SUCCESS) {
        ol_mutex_destroy(&c->mu);
        free(c);
        return NULL;
    }
    
    c->state = OL_PROMISE_PENDING;
    c->value = NULL;
    c->dtor = NULL;
    c->error_code = 0;
    c->value_taken = false;
    c->conts = NULL;
    c->ref_count = 1; /* Initial reference for promise */
    c->loop = loop;
    
    return c;
}

/**
 * @brief Release a core reference
 */
static void ol_core_release(ol_core_t *c) {
    if (!c) {
        return;
    }
    
    /* Free continuations */
    ol_cont_node_t *node = c->conts;
    while (node) {
        ol_cont_node_t *next = node->next;
        free(node);
        node = next;
    }
    
    /* Destroy value if still owned */
    if (c->value && c->dtor && !c->value_taken) {
        c->dtor(c->value);
    }
    
    ol_cond_destroy(&c->cv);
    ol_mutex_destroy(&c->mu);
    free(c);
}

/**
 * @brief Increment core reference count
 */
static void ol_core_ref(ol_core_t *c) {
    ol_mutex_lock(&c->mu);
    c->ref_count++;
    ol_mutex_unlock(&c->mu);
}

/**
 * @brief Decrement core reference count and release if zero
 */
static void ol_core_unref(ol_core_t *c) {
    bool should_free = false;
    
    ol_mutex_lock(&c->mu);
    c->ref_count--;
    if (c->ref_count == 0) {
        should_free = true;
    }
    ol_mutex_unlock(&c->mu);
    
    if (should_free) {
        ol_core_release(c);
    }
}

/**
 * @brief Dispatch continuations
 */
static void ol_core_dispatch(ol_core_t *c) {
    /* Snapshot continuations under lock */
    ol_mutex_lock(&c->mu);
    ol_cont_node_t *list = c->conts;
    c->conts = NULL; /* Consume list */
    
    ol_promise_state_t state = c->state;
    const void *value = c->value_taken ? NULL : c->value;
    int error = c->error_code;
    ol_event_loop_t *loop = c->loop;
    
    ol_mutex_unlock(&c->mu);
    
    /* Wake event loop if attached */
    if (loop) {
        ol_event_loop_wake(loop);
    }
    
    /* Call continuations */
    ol_cont_node_t *node = list;
    while (node) {
        node->cb(loop, state, value, error, node->user_data);
        node = node->next;
    }
    
    /* Free continuation nodes */
    node = list;
    while (node) {
        ol_cont_node_t *next = node->next;
        free(node);
        node = next;
    }
}

/* --------------------------------------------------------------------------
 * Public API: Promise functions
 * -------------------------------------------------------------------------- */

ol_promise_t* ol_promise_create(ol_event_loop_t *loop) {
    ol_core_t *c = ol_core_create(loop);
    if (!c) {
        return NULL;
    }
    
    ol_promise_t *p = (ol_promise_t*)calloc(1, sizeof(ol_promise_t));
    if (!p) {
        ol_core_release(c);
        return NULL;
    }
    
    p->core = c;
    return p;
}

void ol_promise_destroy(ol_promise_t *p) {
    if (!p) {
        return;
    }
    
    if (p->core) {
        ol_core_unref(p->core);
    }
    
    free(p);
}

ol_future_t* ol_promise_get_future(ol_promise_t *p) {
    if (!p || !p->core) {
        return NULL;
    }
    
    ol_future_t *f = (ol_future_t*)calloc(1, sizeof(ol_future_t));
    if (!f) {
        return NULL;
    }
    
    ol_core_ref(p->core);
    f->core = p->core;
    
    return f;
}

int ol_promise_fulfill(ol_promise_t *p, void *value, ol_value_destructor dtor) {
    if (!p || !p->core) {
        if (dtor && value) {
            dtor(value);
        }
        return OL_ERROR;
    }
    
    ol_core_t *c = p->core;
    bool success = false;
    
    ol_mutex_lock(&c->mu);
    
    if (c->state == OL_PROMISE_PENDING) {
        c->state = OL_PROMISE_FULFILLED;
        c->value = value;
        c->dtor = dtor;
        c->error_code = 0;
        success = true;
        
        ol_cond_broadcast(&c->cv);
    }
    
    ol_mutex_unlock(&c->mu);
    
    if (!success) {
        /* Caller still owns value on failure */
        if (dtor && value) {
            dtor(value);
        }
        return OL_ERROR;
    }
    
    ol_core_dispatch(c);
    return OL_SUCCESS;
}

int ol_promise_reject(ol_promise_t *p, int error_code) {
    if (!p || !p->core) {
        return OL_ERROR;
    }
    
    ol_core_t *c = p->core;
    bool success = false;
    
    ol_mutex_lock(&c->mu);
    
    if (c->state == OL_PROMISE_PENDING) {
        c->state = OL_PROMISE_REJECTED;
        c->error_code = error_code;
        success = true;
        
        ol_cond_broadcast(&c->cv);
    }
    
    ol_mutex_unlock(&c->mu);
    
    if (!success) {
        return OL_ERROR;
    }
    
    ol_core_dispatch(c);
    return OL_SUCCESS;
}

int ol_promise_cancel(ol_promise_t *p) {
    if (!p || !p->core) {
        return OL_ERROR;
    }
    
    ol_core_t *c = p->core;
    bool success = false;
    
    ol_mutex_lock(&c->mu);
    
    if (c->state == OL_PROMISE_PENDING) {
        c->state = OL_PROMISE_CANCELED;
        success = true;
        
        ol_cond_broadcast(&c->cv);
    }
    
    ol_mutex_unlock(&c->mu);
    
    if (!success) {
        return OL_ERROR;
    }
    
    ol_core_dispatch(c);
    return OL_SUCCESS;
}

ol_promise_state_t ol_promise_state(const ol_promise_t *p) {
    if (!p || !p->core) {
        return OL_PROMISE_CANCELED;
    }
    
    ol_promise_state_t state;
    
    ol_mutex_lock((ol_mutex_t*)&p->core->mu);
    state = p->core->state;
    ol_mutex_unlock((ol_mutex_t*)&p->core->mu);
    
    return state;
}

bool ol_promise_is_done(const ol_promise_t *p) {
    return ol_promise_state(p) != OL_PROMISE_PENDING;
}

/* --------------------------------------------------------------------------
 * Public API: Future functions
 * -------------------------------------------------------------------------- */

void ol_future_destroy(ol_future_t *f) {
    if (!f) {
        return;
    }
    
    if (f->core) {
        ol_core_unref(f->core);
    }
    
    free(f);
}

int ol_future_await(ol_future_t *f, int64_t deadline_ns) {
    if (!f || !f->core) {
        return OL_ERROR;
    }
    
    ol_core_t *c = f->core;
    
    ol_mutex_lock(&c->mu);
    
    while (c->state == OL_PROMISE_PENDING) {
        int r = ol_cond_wait_until(&c->cv, &c->mu, deadline_ns);
        
        if (r == 1) {
            /* Signaled, check state again */
            continue;
        } else if (r == 0) {
            /* Timeout */
            ol_mutex_unlock(&c->mu);
            return OL_TIMEOUT;
        } else {
            /* Error */
            ol_mutex_unlock(&c->mu);
            return OL_ERROR;
        }
    }
    
    ol_mutex_unlock(&c->mu);
    return 1; /* Completed */
}

int ol_future_then(ol_future_t *f, ol_future_cb cb, void *user_data) {
    if (!f || !f->core || !cb) {
        return OL_ERROR;
    }
    
    ol_core_t *c = f->core;
    
    ol_mutex_lock(&c->mu);
    
    if (c->state == OL_PROMISE_PENDING) {
        /* Add to continuation list */
        ol_cont_node_t *node = (ol_cont_node_t*)calloc(1, sizeof(ol_cont_node_t));
        if (!node) {
            ol_mutex_unlock(&c->mu);
            return OL_ERROR;
        }
        
        node->cb = cb;
        node->user_data = user_data;
        node->next = c->conts;
        c->conts = node;
        
        ol_mutex_unlock(&c->mu);
        return OL_SUCCESS;
    } else {
        /* Already resolved, call immediately */
        ol_promise_state_t state = c->state;
        const void *value = c->value_taken ? NULL : c->value;
        int error = c->error_code;
        ol_event_loop_t *loop = c->loop;
        
        ol_mutex_unlock(&c->mu);
        
        if (loop) {
            ol_event_loop_wake(loop);
        }
        
        cb(loop, state, value, error, user_data);
        return OL_SUCCESS;
    }
}

const void* ol_future_get_value_const(const ol_future_t *f) {
    if (!f || !f->core) {
        return NULL;
    }
    
    const void *value = NULL;
    
    ol_mutex_lock((ol_mutex_t*)&f->core->mu);
    if (f->core->state == OL_PROMISE_FULFILLED && !f->core->value_taken) {
        value = f->core->value;
    }
    ol_mutex_unlock((ol_mutex_t*)&f->core->mu);
    
    return value;
}

void* ol_future_take_value(ol_future_t *f) {
    if (!f || !f->core) {
        return NULL;
    }
    
    void *value = NULL;
    
    ol_mutex_lock(&f->core->mu);
    if (f->core->state == OL_PROMISE_FULFILLED && !f->core->value_taken) {
        value = f->core->value;
        f->core->value = NULL;
        f->core->value_taken = true;
        f->core->dtor = NULL; /* Caller now owns the value */
    }
    ol_mutex_unlock(&f->core->mu);
    
    return value;
}

int ol_future_error(const ol_future_t *f) {
    if (!f || !f->core) {
        return 0;
    }
    
    int error = 0;
    
    ol_mutex_lock((ol_mutex_t*)&f->core->mu);
    if (f->core->state == OL_PROMISE_REJECTED) {
        error = f->core->error_code;
    }
    ol_mutex_unlock((ol_mutex_t*)&f->core->mu);
    
    return error;
}

ol_promise_state_t ol_future_state(const ol_future_t *f) {
    if (!f || !f->core) {
        return OL_PROMISE_CANCELED;
    }
    
    ol_promise_state_t state;
    
    ol_mutex_lock((ol_mutex_t*)&f->core->mu);
    state = f->core->state;
    ol_mutex_unlock((ol_mutex_t*)&f->core->mu);
    
    return state;
}