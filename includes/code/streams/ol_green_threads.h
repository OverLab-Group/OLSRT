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

static void ol_gt_trampoline(uintptr_t arg_low, uintptr_t arg_high);

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

#ifdef __linux__

typedef enum {
    OL_GT_NEW = 0,
    OL_GT_READY,
    OL_GT_RUNNING,
    OL_GT_DONE,
    OL_GT_CANCELED
} ol_gt_state_t;

typedef struct ol_gt_ctx {
    #if defined(__x86_64__)
    void *rbx, *rbp, *r12, *r13, *r14, *r15;
    void *rsp;
    void *rip;
    #elif defined(__i386__)
    void *ebx, *ebp, *esi, *edi;
    void *esp;
    void *eip;
    #elif defined(__aarch64__)
    void *x19, *x20, *x21, *x22, *x23, *x24, *x25, *x26, *x27, *x28;
    void *x29; /* frame pointer */
    void *sp;
    void *lr;  /* link register */
    void *pc;  /* program counter */
    #elif defined(__arm__)
    void *r4, *r5, *r6, *r7, *r8, *r9, *r10;
    void *r11; /* frame pointer */
    void *sp;
    void *lr;  /* link register */
    void *pc;  /* program counter */
    #else
    sigjmp_buf env;
    #endif
    /* Stack information */
    void *stack;
    size_t stack_size;
    int is_main;
} ol_gt_ctx_t;

typedef struct ol_gt {
    ol_gt_state_t state;
    void *arg;
    ol_gt_entry_fn entry;

    /* Context and stack */
    ol_gt_ctx_t ctx;
    void *stack;
    size_t stack_size;

    /* Cooperative join flag */
    bool joined;

    /* Cancellation flag */
    volatile int cancel_flag;

    /* Linking in scheduler list */
    struct ol_gt *next;
} ol_gt_t;

/* Scheduler context */
typedef struct {
    ol_gt_ctx_t sched_ctx;  /* Context scheduler */
    /* Ready list */
    ol_gt_t *ready_head;
    ol_gt_t *ready_tail;
    /* Currently running GT */
    ol_gt_t *current;
    /* Default stack size */
    size_t default_stack;
    /* Initialized flag */
    bool initialized;
} ol_gt_sched_t;

static __thread ol_gt_sched_t g_sched = {0};

static inline void ol_ctx_restore(ol_gt_ctx_t *ctx) __attribute__((noreturn));

#endif




#ifdef __cplusplus
}
#endif

#endif /* OL_GREEN_THREADS_H */
