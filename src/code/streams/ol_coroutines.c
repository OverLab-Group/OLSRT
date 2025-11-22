#include "ol_coroutines.h"

#include <stdlib.h>
#include <string.h>

/* Coroutine states */
typedef enum {
    CO_NEW = 0,
    CO_READY,
    CO_RUNNING,
    CO_DONE,
    CO_CANCELED
} co_state_t;

/* Coroutine handle built on a single green thread */
struct ol_co {
    /* Backend GT */
    ol_gt_t *gt;

    /* Entry adapter */
    ol_co_entry_fn entry;
    void *arg;

    /* State */
    co_state_t state;
    bool joined;

    /* Cooperative cancellation flag mirrors GT cancellation */
    volatile int canceled;

    /* Payload exchange:
     * - resume_payload: provided by caller when resuming
     * - yield_payload:  provided by coroutine when yielding
     * Exchange protected by a small mutex to ensure consistent handoff.
     */
    ol_mutex_t mu;
    void *resume_payload;
    void *yield_payload;

    /* Final result when entry returns */
    void *result;
};

/* Thread-local: the currently running coroutine (inside coroutine context) */
#if defined(_WIN32)
static __declspec(thread) struct ol_co *g_current_co = NULL;
#else
static __thread struct ol_co *g_current_co = NULL;
#endif

/* Scheduler wrappers */

int ol_coroutine_scheduler_init(void) {
    return ol_gt_scheduler_init();
}

void ol_coroutine_scheduler_shutdown(void) {
    ol_gt_scheduler_shutdown();
}

/* Coroutine entry trampoline: runs inside the green thread context */
static void co_trampoline(void *arg) {
    ol_co_t *co = (ol_co_t*)arg;
    g_current_co = co;
    co->state = CO_RUNNING;

    /* Run user entry if not canceled before start */
    if (!co->canceled) {
        co->result = co->entry(co->arg);
    } else {
        co->state = CO_CANCELED;
        g_current_co = NULL;
        ol_gt_yield(); /* return control to scheduler/caller */
        return;
    }

    /* Mark done and yield back control one final time to allow joiners to collect result */
    co->state = CO_DONE;
    g_current_co = NULL;
    ol_gt_yield();
}

/* Public API */

ol_co_t* ol_co_spawn(ol_co_entry_fn entry, void *arg, size_t stack_size) {
    if (!entry) return NULL;

    ol_co_t *co = (ol_co_t*)calloc(1, sizeof(ol_co_t));
    if (!co) return NULL;

    co->entry = entry;
    co->arg = arg;
    co->state = CO_NEW;
    co->joined = false;
    co->canceled = 0;
    co->resume_payload = NULL;
    co->yield_payload = NULL;
    co->result = NULL;

    if (ol_mutex_init(&co->mu) != 0) {
        free(co);
        return NULL;
    }

    /* Spawn the underlying green thread */
    co->gt = ol_gt_spawn(co_trampoline, co, stack_size);
    if (!co->gt) {
        ol_mutex_destroy(&co->mu);
        free(co);
        return NULL;
    }

    co->state = CO_READY;
    return co;
}

int ol_co_resume(ol_co_t *co, void *payload) {
    if (!co || !co->gt) return -1;
    if (co->state == CO_DONE || co->state == CO_CANCELED) return -1;

    /* Store payload to be observed by coroutine on next ol_co_yield() */
    ol_mutex_lock(&co->mu);
    co->resume_payload = payload;
    ol_mutex_unlock(&co->mu);

    /* Resume the underlying green thread cooperatively */
    int r = ol_gt_resume(co->gt);
    if (r < 0) return -1;

    /* After resume returns, if coroutine yielded, its yield_payload is ready.
     * Callers can read it via subsequent API, but we keep it internally until join or next resume.
     */
    return 0;
}

/* Yield from inside coroutine, passing payload to the resuming caller.
 * Returns the latest resume payload (or NULL) provided by caller via ol_co_resume().
 */
void* ol_co_yield(void *payload) {
    ol_co_t *co = g_current_co;
    if (!co) return NULL;

    /* Write yield payload */
    ol_mutex_lock(&co->mu);
    co->yield_payload = payload;
    /* Read resume payload to return to coroutine after switch back */
    void *incoming = co->resume_payload;
    /* Clear resume payload for next round */
    co->resume_payload = NULL;
    ol_mutex_unlock(&co->mu);

    /* Switch back to scheduler/caller */
    ol_gt_yield();

    /* When resuming, provide the last caller payload to coroutine */
    return incoming;
}

/* Join cooperatively until coroutine completes; return final result. */
void* ol_co_join(ol_co_t *co) {
    if (!co || !co->gt) return NULL;
    if (co->joined) return co->result;

    int r = ol_gt_join(co->gt);
    if (r < 0) {
        /* In cooperative model, join can fail if no one yields; treat as best-effort */
        return NULL;
    }
    co->joined = true;
    return co->result;
}

/* Cancel: coroutine should check ol_co_is_canceled(), e.g., between yields or in loops. */
int ol_co_cancel(ol_co_t *co) {
    if (!co || !co->gt) return -1;
    if (co->state == CO_DONE || co->state == CO_CANCELED) return -1;
    co->canceled = 1;
    (void)ol_gt_cancel(co->gt);
    co->state = CO_CANCELED;
    return 0;
}

/* Introspection */

bool ol_co_is_alive(const ol_co_t *co) {
    if (!co) return false;
    return co->state != CO_DONE && co->state != CO_CANCELED;
}

bool ol_co_is_canceled(const ol_co_t *co) {
    if (!co) return false;
    return co->canceled != 0;
}

/* Destroy handle */
void ol_co_destroy(ol_co_t *co) {
    if (!co) return;
    /* Ensure coroutine not running */
    if (co->state != CO_DONE && co->state != CO_CANCELED) {
        (void)ol_co_cancel(co);
        (void)ol_co_join(co);
    }
    if (co->gt) {
        ol_gt_destroy(co->gt);
        co->gt = NULL;
    }
    ol_mutex_destroy(&co->mu);
    free(co);
}
