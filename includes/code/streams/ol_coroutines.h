#ifndef OL_COROUTINES_H
#define OL_COROUTINES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_green_threads.h"
#include "ol_lock_mutex.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle */
typedef struct ol_co ol_co_t;

/* Entry function signature.
 * Return value is the final "result" of the coroutine (delivered to join).
 */
typedef void* (*ol_co_entry_fn)(void *arg);

/* Initialize/shutdown the coroutine scheduler for the current OS thread.
 * These wrap ol_gt_scheduler_* and are idempotent.
 */
int  ol_coroutine_scheduler_init(void);
void ol_coroutine_scheduler_shutdown(void);

/* Spawn a coroutine with an optional stack size (0 => default).
 * Returns a coroutine handle or NULL on failure.
 */
ol_co_t* ol_co_spawn(ol_co_entry_fn entry, void *arg, size_t stack_size);

/* Resume a coroutine, passing a payload that the coroutine can receive via ol_co_yield().
 * Returns 0 on success, negative on error.
 */
int  ol_co_resume(ol_co_t *co, void *payload);

/* Yield from within a coroutine back to the caller, passing a payload.
 * Returns the next payload passed by the next resume (or NULL if none).
 */
void* ol_co_yield(void *payload);

/* Join: wait cooperatively until the coroutine completes.
 * Returns the coroutine's final return value.
 */
void* ol_co_join(ol_co_t *co);

/* Cancel: cooperative cancellation; the coroutine must check ol_co_is_canceled().
 * Returns 0 on success, negative on error.
 */
int  ol_co_cancel(ol_co_t *co);

/* Introspection */
bool  ol_co_is_alive(const ol_co_t *co);
bool  ol_co_is_canceled(const ol_co_t *co);

/* Destroy a coroutine handle and free resources.
 * Only call after completion or cancellation+completion.
 */
void  ol_co_destroy(ol_co_t *co);

#ifdef __cplusplus
}
#endif

#endif /* OL_COROUTINES_H */
