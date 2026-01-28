#ifndef OL_GREEN_THREADS_H
#define OL_GREEN_THREADS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle */
typedef struct ol_gt ol_gt_t;

/* Function signature for green thread entry */
typedef void (*ol_gt_entry_fn)(void *arg);

/* Scheduler lifecycle: binds to the calling OS thread.
 * Returns 0 on success, negative on failure.
 */
int  ol_gt_scheduler_init(void);
void ol_gt_scheduler_shutdown(void);

/* Spawn a new green thread.
 * - entry: function to run
 * - arg: user context passed to entry
 * - stack_size: desired stack size in bytes (use 0 for default: 256 KiB)
 * Returns handle or NULL on failure.
 */
ol_gt_t* ol_gt_spawn(ol_gt_entry_fn entry, void *arg, size_t stack_size);

/* Resume a suspended/ready green thread.
 * Returns 0 on success, negative on error (already finished or invalid state).
 */
int  ol_gt_resume(ol_gt_t *gt);

/* Yield from the currently running green thread back to the scheduler.
 * Must be called from inside a green thread.
 */
void ol_gt_yield(void);

/* Join: wait until the green thread completes (cooperatively).
 * Returns 0 on success, negative on error.
 */
int  ol_gt_join(ol_gt_t *gt);

/* Cancel: request cooperative cancellation.
 * The thread must periodically yield or return; cancel does not preempt.
 * Returns 0 on success, negative if already finished.
 */
int  ol_gt_cancel(ol_gt_t *gt);

/* Introspection */
bool ol_gt_is_alive(const ol_gt_t *gt);
bool ol_gt_is_canceled(const ol_gt_t *gt);

/* Get the currently running green thread handle, or NULL if in scheduler. */
ol_gt_t* ol_gt_current(void);

/* Destroy the handle and free resources (stack and control block).
 * Only call after the thread has completed (joined) or canceled and finished.
 */
void ol_gt_destroy(ol_gt_t *gt);

static inline void ol_ctx_restore(ol_gt_ctx_t *ctx) __attribute__((noreturn));


#ifdef __cplusplus
}
#endif

#endif /* OL_GREEN_THREADS_H */
