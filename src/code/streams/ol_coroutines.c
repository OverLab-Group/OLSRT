/**
 * @file ol_coroutines.c
 * @brief Coroutine (user-level green threads) abstraction built on platform-specific GT backend.
 *
 * Overview
 * --------
 * - Provides a coroutine handle (ol_co_t) that runs on a single-threaded cooperative scheduler.
 * - Exposes spawn/resume/yield/join/cancel semantics.
 * - Uses platform-specific backends:
 *     * POSIX: ucontext (getcontext/makecontext/swapcontext)
 *     * Windows: Fibers (ConvertThreadToFiber/CreateFiber/SwitchToFiber)
 *
 * Design notes
 * ------------
 * - Coroutines are cooperative: they yield explicitly via ol_co_yield.
 * - Cancellation is cooperative: ol_co_cancel sets a flag; coroutine must check ol_co_is_canceled.
 * - The scheduler is single-threaded per OS thread; ol_co_spawn requires scheduler initialization.
 *
 * Safety and testing
 * ------------------
 * - Use TSan to detect races when coroutines interact with shared data across OS threads.
 * - ASan/Valgrind help detect stack/heap misuse; ensure stack_size is reasonable.
 * - Unit tests should exercise resume/yield/join/cancel sequences and payload passing.
 *
 * API contracts
 * -------------
 * - ol_co_spawn: returns a coroutine handle; caller must ol_co_destroy when done.
 * - ol_co_resume: resumes coroutine; returns 0 on success, -1 on error.
 * - ol_co_yield: called inside coroutine to yield back to caller; returns resume payload.
 * - ol_co_join: cooperatively wait until coroutine completes; returns final result.
 * - ol_co_cancel: request cancellation; coroutine must observe via ol_co_is_canceled.
 */

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

/**
 * @struct ol_co
 * @brief Coroutine handle structure.
 *
 * Fields:
 * - gt: underlying green thread handle (platform-specific)
 * - entry: user entry function
 * - arg: argument passed to entry
 * - state: coroutine lifecycle state
 * - joined: whether join has been performed
 * - canceled: cooperative cancellation flag
 * - mu: mutex protecting payload exchange
 * - resume_payload: payload provided by caller on resume
 * - yield_payload: payload provided by coroutine on yield
 * - result: final result when entry returns
 *
 * Notes:
 * - Payload exchange is protected by mu to ensure consistent handoff between caller and coroutine.
 */
struct ol_co {
    ol_gt_t *gt;
    ol_co_entry_fn entry;
    void *arg;
    co_state_t state;
    bool joined;
    volatile int canceled;
    ol_mutex_t mu;
    void *resume_payload;
    void *yield_payload;
    void *result;
};

/* Thread-local pointer to currently running coroutine (inside coroutine context) */
#if defined(_WIN32)
static __declspec(thread) struct ol_co *g_current_co = NULL;
#else
static __thread struct ol_co *g_current_co = NULL;
#endif

/* -------------------- Scheduler wrappers -------------------- */

/**
 * @brief Initialize the coroutine scheduler (platform-specific).
 *
 * Returns 0 on success, -1 on failure.
 */
int ol_coroutine_scheduler_init(void) {
    return ol_gt_scheduler_init();
}

/**
 * @brief Shutdown the coroutine scheduler (platform-specific).
 */
void ol_coroutine_scheduler_shutdown(void) {
    ol_gt_scheduler_shutdown();
}

/* -------------------- Trampoline and lifecycle -------------------- */

/**
 * @brief Trampoline that runs inside the green thread context and executes user entry.
 *
 * - Sets g_current_co
 * - Runs entry and stores result
 * - Marks state as DONE and yields back to scheduler for joiners to collect result
 *
 * @param arg ol_co_t* pointer
 */
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

/* -------------------- Public API -------------------- */

/**
 * @brief Spawn a new coroutine.
 *
 * - entry: function to run inside coroutine (must be non-NULL)
 * - arg: argument passed to entry
 * - stack_size: advisory stack size for POSIX ucontext or fiber creation
 *
 * Returns a new ol_co_t* or NULL on failure.
 */
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

    /* Spawn underlying green thread (platform-specific) */
    co->gt = ol_gt_spawn(co_trampoline, co, stack_size);
    if (!co->gt) {
        ol_mutex_destroy(&co->mu);
        free(co);
        return NULL;
    }

    co->state = CO_READY;
    return co;
}

/**
 * @brief Resume coroutine, providing an optional payload to the coroutine.
 *
 * - Returns 0 on success, -1 on error (invalid handle or coroutine already done/canceled)
 *
 * @param co Coroutine handle
 * @param payload Payload to pass to coroutine (may be NULL)
 * @return int status
 */
int ol_co_resume(ol_co_t *co, void *payload) {
    if (!co || !co->gt) return -1;
    if (co->state == CO_DONE || co->state == CO_CANCELED) return -1;

    /* Store payload to be observed by coroutine on next ol_co_yield() */
    ol_mutex_lock(&co->mu);
    co->resume_payload = payload;
    ol_mutex_unlock(&co->mu);

    /* Resume underlying green thread cooperatively */
    int r = ol_gt_resume(co->gt);
    if (r < 0) return -1;

    /* After resume returns, if coroutine yielded, its yield_payload is ready.
     * Callers can read it via subsequent API, but we keep it internally until join or next resume.
     */
    return 0;
}

/**
 * @brief Yield from inside coroutine, passing payload to the resuming caller.
 *
 * - Called only from inside coroutine context.
 * - Returns the latest resume payload provided by caller via ol_co_resume.
 *
 * @param payload Payload to yield to caller (may be NULL)
 * @return void* resume payload for coroutine (may be NULL)
 */
void* ol_co_yield(void *payload) {
    ol_co_t *co = g_current_co;
    if (!co) return NULL;

    /* Write yield payload and read resume payload atomically */
    ol_mutex_lock(&co->mu);
    co->yield_payload = payload;
    void *incoming = co->resume_payload;
    co->resume_payload = NULL;
    ol_mutex_unlock(&co->mu);

    /* Switch back to scheduler/caller */
    ol_gt_yield();

    /* When resuming, provide the last caller payload to coroutine */
    return incoming;
}

/**
 * @brief Join cooperatively until coroutine completes; return final result.
 *
 * - Returns result pointer (as returned by entry) or NULL on error.
 * - Marks coroutine as joined to avoid double-join semantics.
 *
 * @param co Coroutine handle
 * @return void* final result or NULL
 */
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

/**
 * @brief Request cancellation of coroutine.
 *
 * - Cancellation is cooperative: coroutine should check ol_co_is_canceled between yields.
 * - Returns 0 on success, -1 on invalid handle or already finished.
 *
 * @param co Coroutine handle
 * @return int status
 */
int ol_co_cancel(ol_co_t *co) {
    if (!co || !co->gt) return -1;
    if (co->state == CO_DONE || co->state == CO_CANCELED) return -1;
    co->canceled = 1;
    (void)ol_gt_cancel(co->gt);
    co->state = CO_CANCELED;
    return 0;
}

/* -------------------- Introspection and cleanup -------------------- */

/**
 * @brief Check whether coroutine is alive (not done and not canceled).
 *
 * @param co Coroutine handle
 * @return bool true if alive, false otherwise
 */
bool ol_co_is_alive(const ol_co_t *co) {
    if (!co) return false;
    return co->state != CO_DONE && co->state != CO_CANCELED;
}

/**
 * @brief Check whether coroutine has been requested to cancel.
 *
 * @param co Coroutine handle
 * @return bool true if canceled, false otherwise
 */
bool ol_co_is_canceled(const ol_co_t *co) {
    if (!co) return false;
    return co->canceled != 0;
}

/**
 * @brief Destroy coroutine handle and free resources.
 *
 * - Ensures coroutine is not running; attempts cooperative cancel and join if necessary.
 *
 * @param co Coroutine handle
 */
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