/**
 * @file ol_coroutines.c
 * @brief User-level coroutine implementation for OLSRT
 * @version 1.2.0
 * 
 * This module implements cooperative coroutines (green threads)
 * that work across Linux, Windows, macOS, and BSD systems.
 */

#include "ol_coroutines.h"
#include "ol_common.h"
#include "ol_lock_mutex.h"

#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Coroutine state definitions
 * -------------------------------------------------------------------------- */

/**
 * @brief Coroutine states
 */
typedef enum {
    CO_STATE_NEW,       /**< Created but not started */
    CO_STATE_READY,     /**< Ready to run */
    CO_STATE_RUNNING,   /**< Currently running */
    CO_STATE_SUSPENDED, /**< Suspended (yielded) */
    CO_STATE_DONE,      /**< Completed execution */
    CO_STATE_CANCELED   /**< Canceled */
} ol_co_state_t;

/**
 * @brief Coroutine structure
 */
struct ol_co {
    /* Green thread backend */
    ol_gt_t *gt;                    /**< Underlying green thread */
    
    /* User function and data */
    ol_co_entry_fn entry;           /**< User entry function */
    void *arg;                      /**< Entry function argument */
    
    /* State management */
    ol_co_state_t state;            /**< Current coroutine state */
    bool joined;                    /**< Whether join has been called */
    volatile int canceled;          /**< Cancellation requested flag */
    
    /* Payload exchange */
    ol_mutex_t payload_mutex;       /**< Protects payload exchange */
    void *resume_payload;           /**< Payload from resume() */
    void *yield_payload;            /**< Payload from yield() */
    void *result;                   /**< Final result from entry */
    
    /* Statistics */
    uint64_t yield_count;           /**< Number of times yielded */
    uint64_t resume_count;          /**< Number of times resumed */
};

/* --------------------------------------------------------------------------
 * Thread-local current coroutine
 * -------------------------------------------------------------------------- */

/**
 * @brief Thread-local pointer to currently running coroutine
 */
static OL_THREAD_LOCAL ol_co_t *g_current_coroutine = NULL;

/**
 * @brief Get current coroutine
 */
static ol_co_t* ol_get_current_coroutine(void) {
    return g_current_coroutine;
}

/**
 * @brief Set current coroutine
 */
static void ol_set_current_coroutine(ol_co_t *co) {
    g_current_coroutine = co;
}

/* --------------------------------------------------------------------------
 * Trampoline and helper functions
 * -------------------------------------------------------------------------- */

/**
 * @brief Coroutine trampoline (runs in green thread context)
 */
static void ol_co_trampoline(void *arg) {
    ol_co_t *co = (ol_co_t*)arg;
    if (!co) {
        return;
    }
    
    /* Set as current coroutine */
    ol_set_current_coroutine(co);
    co->state = CO_STATE_RUNNING;
    
    /* Check for cancellation before starting */
    if (co->canceled) {
        co->state = CO_STATE_CANCELED;
        ol_set_current_coroutine(NULL);
        ol_gt_yield();
        return;
    }
    
    /* Run user entry function */
    if (co->entry) {
        co->result = co->entry(co->arg);
    }
    
    /* Mark as done */
    co->state = CO_STATE_DONE;
    ol_set_current_coroutine(NULL);
    
    /* Final yield to return control */
    ol_gt_yield();
}

/* --------------------------------------------------------------------------
 * Public API implementation
 * -------------------------------------------------------------------------- */

int ol_coroutine_scheduler_init(void) {
    return ol_gt_scheduler_init();
}

void ol_coroutine_scheduler_shutdown(void) {
    ol_gt_scheduler_shutdown();
}

ol_co_t* ol_co_spawn(ol_co_entry_fn entry,
                     void *arg,
                     size_t stack_size) {
    if (!entry) {
        return NULL;
    }
    
    ol_co_t *co = (ol_co_t*)calloc(1, sizeof(ol_co_t));
    if (!co) {
        return NULL;
    }
    
    co->entry = entry;
    co->arg = arg;
    co->state = CO_STATE_NEW;
    co->joined = false;
    co->canceled = 0;
    co->resume_payload = NULL;
    co->yield_payload = NULL;
    co->result = NULL;
    co->yield_count = 0;
    co->resume_count = 0;
    
    /* Initialize payload mutex */
    if (ol_mutex_init(&co->payload_mutex) != OL_SUCCESS) {
        free(co);
        return NULL;
    }
    
    /* Spawn underlying green thread */
    co->gt = ol_gt_spawn(ol_co_trampoline, co, stack_size);
    if (!co->gt) {
        ol_mutex_destroy(&co->payload_mutex);
        free(co);
        return NULL;
    }
    
    co->state = CO_STATE_READY;
    return co;
}

int ol_co_resume(ol_co_t *co, void *payload) {
    if (!co || !co->gt) {
        return OL_ERROR;
    }
    
    /* Check if coroutine can be resumed */
    if (co->state == CO_STATE_DONE || 
        co->state == CO_STATE_CANCELED ||
        co->joined) {
        return OL_ERROR;
    }
    
    /* Store resume payload */
    ol_mutex_lock(&co->payload_mutex);
    co->resume_payload = payload;
    co->resume_count++;
    ol_mutex_unlock(&co->payload_mutex);
    
    /* Resume green thread */
    if (ol_gt_resume(co->gt) != OL_SUCCESS) {
        return OL_ERROR;
    }
    
    /* Update state */
    if (co->state == CO_STATE_NEW || co->state == CO_STATE_READY) {
        co->state = CO_STATE_RUNNING;
    } else if (co->state == CO_STATE_SUSPENDED) {
        co->state = CO_STATE_RUNNING;
    }
    
    return OL_SUCCESS;
}

void* ol_co_yield(void *payload) {
    ol_co_t *co = ol_get_current_coroutine();
    if (!co) {
        return NULL;
    }
    
    /* Store yield payload and get resume payload */
    void *resume_payload = NULL;
    
    ol_mutex_lock(&co->payload_mutex);
    co->yield_payload = payload;
    co->yield_count++;
    resume_payload = co->resume_payload;
    co->resume_payload = NULL;
    ol_mutex_unlock(&co->payload_mutex);
    
    /* Update state and yield */
    co->state = CO_STATE_SUSPENDED;
    ol_gt_yield();
    
    return resume_payload;
}

void* ol_co_join(ol_co_t *co) {
    if (!co || !co->gt) {
        return NULL;
    }
    
    if (co->joined) {
        return co->result;
    }
    
    /* Join underlying green thread */
    if (ol_gt_join(co->gt) != OL_SUCCESS) {
        return NULL;
    }
    
    co->joined = true;
    return co->result;
}

int ol_co_cancel(ol_co_t *co) {
    if (!co || !co->gt) {
        return OL_ERROR;
    }
    
    if (co->state == CO_STATE_DONE || 
        co->state == CO_STATE_CANCELED) {
        return OL_ERROR;
    }
    
    /* Request cancellation */
    co->canceled = 1;
    
    /* Cancel underlying green thread */
    if (ol_gt_cancel(co->gt) != OL_SUCCESS) {
        return OL_ERROR;
    }
    
    co->state = CO_STATE_CANCELED;
    return OL_SUCCESS;
}

void ol_co_destroy(ol_co_t *co) {
    if (!co) {
        return;
    }
    
    /* Cancel if still running */
    if (co->state != CO_STATE_DONE && 
        co->state != CO_STATE_CANCELED &&
        !co->joined) {
        ol_co_cancel(co);
        ol_co_join(co);
    }
    
    /* Destroy green thread */
    if (co->gt) {
        ol_gt_destroy(co->gt);
    }
    
    /* Destroy mutex */
    ol_mutex_destroy(&co->payload_mutex);
    
    /* Free structure */
    free(co);
}

bool ol_co_is_alive(const ol_co_t *co) {
    if (!co) {
        return false;
    }
    
    return (co->state != CO_STATE_DONE && 
            co->state != CO_STATE_CANCELED);
}

bool ol_co_is_canceled(const ol_co_t *co) {
    if (!co) {
        return false;
    }
    
    return (co->canceled != 0);
}