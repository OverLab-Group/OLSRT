#include "ol_green_threads.h"

#include <stdlib.h>
#include <string.h>

/* Platform selection */
#if defined(_WIN32)
/* --------------------------- Windows (Fibers) --------------------------- */
#include <windows.h>

typedef enum {
    OL_GT_NEW = 0,
    OL_GT_READY,
    OL_GT_RUNNING,
    OL_GT_DONE,
    OL_GT_CANCELED
} ol_gt_state_t;

typedef struct ol_gt {
    ol_gt_state_t state;
    void *arg;
    ol_gt_entry_fn entry;

    /* Fiber */
    LPVOID fiber;

    /* Cooperative join flag */
    bool joined;

    /* Cancellation flag */
    volatile LONG cancel_flag;

    /* Linking in scheduler list */
    struct ol_gt *next;
} ol_gt_t;

/* Scheduler single-threaded context */
typedef struct {
    /* The scheduler runs on the calling thread converted to a fiber. */
    LPVOID scheduler_fiber;
    /* Ready list */
    ol_gt_t *ready_head;
    ol_gt_t *ready_tail;
    /* Currently running GT */
    ol_gt_t *current;
    /* Default stack size unused (Fibers use the thread stack); kept for API parity */
    size_t default_stack;
    /* Initialized flag */
    bool initialized;
} ol_gt_sched_t;

static __declspec(thread) ol_gt_sched_t g_sched = {0};

/* Utility: enqueue a GT in ready list */
static void enqueue_ready(ol_gt_t *gt) {
    gt->next = NULL;
    if (!g_sched.ready_tail) {
        g_sched.ready_head = g_sched.ready_tail = gt;
    } else {
        g_sched.ready_tail->next = gt;
        g_sched.ready_tail = gt;
    }
}

/* Utility: pop next ready GT */
static ol_gt_t* dequeue_ready(void) {
    ol_gt_t *gt = g_sched.ready_head;
    if (gt) {
        g_sched.ready_head = gt->next;
        if (!g_sched.ready_head) g_sched.ready_tail = NULL;
        gt->next = NULL;
    }
    return gt;
}

/* Fiber entry trampoline */
static VOID CALLBACK ol_fiber_start(LPVOID param) {
    ol_gt_t *gt = (ol_gt_t*)param;
    g_sched.current = gt;
    gt->state = OL_GT_RUNNING;

    /* If canceled before start, skip execution */
    if (InterlockedCompareExchange(&gt->cancel_flag, 0, 0) != 0) {
        gt->state = OL_GT_CANCELED;
        g_sched.current = NULL;
        /* Yield back to scheduler */
        SwitchToFiber(g_sched.scheduler_fiber);
        return;
    }

    gt->entry(gt->arg);

    gt->state = OL_GT_DONE;
    g_sched.current = NULL;
    /* Return control to scheduler */
    SwitchToFiber(g_sched.scheduler_fiber);
}

int ol_gt_scheduler_init(void) {
    if (g_sched.initialized) return 0;
    /* Convert calling thread into a fiber to allow switching */
    LPVOID self = ConvertThreadToFiber(NULL);
    if (!self) return -1;
    g_sched.scheduler_fiber = self;
    g_sched.ready_head = g_sched.ready_tail = NULL;
    g_sched.current = NULL;
    g_sched.default_stack = 256 * 1024;
    g_sched.initialized = true;
    return 0;
}

void ol_gt_scheduler_shutdown(void) {
    if (!g_sched.initialized) return;
    /* Drain ready list: mark residual as canceled (cooperative cleanup) */
    ol_gt_t *gt = g_sched.ready_head;
    while (gt) {
        ol_gt_t *nx = gt->next;
        InterlockedExchange(&gt->cancel_flag, 1);
        gt = nx;
    }
    g_sched.ready_head = g_sched.ready_tail = NULL;
    g_sched.current = NULL;
    g_sched.initialized = false;
    /* Convert fiber back to thread. Windows provides ConvertFiberToThread(). */
    ConvertFiberToThread();
    g_sched.scheduler_fiber = NULL;
}

ol_gt_t* ol_gt_spawn(ol_gt_entry_fn entry, void *arg, size_t stack_size) {
    if (!g_sched.initialized || !entry) return NULL;

    ol_gt_t *gt = (ol_gt_t*)calloc(1, sizeof(ol_gt_t));
    if (!gt) return NULL;

    gt->state = OL_GT_NEW;
    gt->arg = arg;
    gt->entry = entry;
    gt->joined = false;
    gt->cancel_flag = 0;
    gt->next = NULL;

    /* Create a fiber; stack_size is advisory only when creating a new fiber via CreateFiber.
     * We pass stack_size if provided, else default.
     */
    SIZE_T sz = (stack_size && stack_size >= (64 * 1024)) ? (SIZE_T)stack_size : (SIZE_T)g_sched.default_stack;
    gt->fiber = CreateFiber(sz, ol_fiber_start, gt);
    if (!gt->fiber) {
        free(gt);
        return NULL;
    }

    gt->state = OL_GT_READY;
    enqueue_ready(gt);
    return gt;
}

/* Run next ready task cooperatively; internal helper */
static int ol_run_next(void) {
    ol_gt_t *gt = dequeue_ready();
    if (!gt) return 0; /* nothing to run */
    /* Switch to fiber */
    SwitchToFiber(gt->fiber);
    /* After fiber returns control to scheduler: if still runnable and not done/canceled, re-enqueue */
    if (gt->state == OL_GT_RUNNING) {
        /* A running fiber should yield to scheduler; if control got back without state change, treat as yield */
        gt->state = OL_GT_READY;
    }
    if (gt->state == OL_GT_READY) {
        enqueue_ready(gt);
    }
    return 1;
}

int ol_gt_resume(ol_gt_t *gt) {
    if (!g_sched.initialized || !gt) return -1;
    if (gt->state == OL_GT_DONE || gt->state == OL_GT_CANCELED) return -1;

    /* If it's newly ready, ensure in ready queue; otherwise will run when scheduled */
    if (gt->state == OL_GT_READY) {
        /* Cooperative scheduler: run exactly this GT now */
        /* Put it at head by crafting it as the next to run */
        /* Simple: switch to fiber directly */
        SwitchToFiber(gt->fiber);
        if (gt->state == OL_GT_RUNNING) gt->state = OL_GT_READY;
        return 0;
    }

    /* If NEW, it's already queued from spawn; run next */
    if (gt->state == OL_GT_NEW || gt->state == OL_GT_RUNNING) {
        /* Running cannot be resumed; NEW will be scheduled */
        return (ol_run_next() >= 0) ? 0 : -1;
    }

    return 0;
}

void ol_gt_yield(void) {
    if (!g_sched.initialized) return;
    ol_gt_t *cur = g_sched.current;
    if (!cur) return; /* not in a green thread */
    /* Mark as ready and switch back to scheduler */
    cur->state = OL_GT_READY;
    SwitchToFiber(g_sched.scheduler_fiber);
}

int ol_gt_join(ol_gt_t *gt) {
    if (!g_sched.initialized || !gt) return -1;
    /* Cooperatively run until target finishes */
    while (gt->state != OL_GT_DONE && gt->state != OL_GT_CANCELED) {
        /* If there are ready tasks, run them; otherwise, if target is running, yield awaits */
        if (!ol_run_next()) {
            /* No ready tasks; if target not finished, this means deadlock (no one yields).
             * In cooperative model, we can't forcibly preempt; return error.
             */
            return -1;
        }
    }
    gt->joined = true;
    return 0;
}

int ol_gt_cancel(ol_gt_t *gt) {
    if (!gt) return -1;
    if (gt->state == OL_GT_DONE || gt->state == OL_GT_CANCELED) return -1;
    InterlockedExchange(&gt->cancel_flag, 1);
    /* If currently running, it will observe cancellation only if entry checks or on next yield.
     * We nudge scheduler to run it soon by placing back to ready list if not running.
     */
    if (gt->state == OL_GT_READY || gt->state == OL_GT_NEW) {
        /* ensure it's in ready list; spawn already enqueues, so no-op */
    }
    return 0;
}

bool ol_gt_is_alive(const ol_gt_t *gt) {
    if (!gt) return false;
    return gt->state != OL_GT_DONE && gt->state != OL_GT_CANCELED;
}

bool ol_gt_is_canceled(const ol_gt_t *gt) {
    if (!gt) return false;
    return InterlockedCompareExchange((LONG*)&gt->cancel_flag, 0, 0) != 0;
}

ol_gt_t* ol_gt_current(void) {
    return g_sched.current;
}

/* Windows: destroy — upgraded to ensure fiber finished before DeleteFiber */
void ol_gt_destroy(ol_gt_t *gt) {
    if (!gt) return;
    if (gt->state != OL_GT_DONE && gt->state != OL_GT_CANCELED) {
        InterlockedExchange(&gt->cancel_flag, 1);
        /* Cooperative join attempt */
        (void)ol_gt_join(gt);
    }
    if (gt->fiber) {
        DeleteFiber(gt->fiber);
        gt->fiber = NULL;
    }
    free(gt);
}

/* --------------------------- End Windows (Fibers) --------------------------- */

#else
/* --------------------------- POSIX --------------------------- */
/* --------------------------- POSIX (Context Switching) --------------------------- */
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>

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

static __thread ol_gt_sched_t g_sched = {0};

static inline void ol_ctx_save(ol_gt_ctx_t *ctx) {
    #if defined(__x86_64__)
    asm volatile (
        "movq %%rbx, 0(%0)\n\t"
        "movq %%rbp, 8(%0)\n\t"
        "movq %%r12, 16(%0)\n\t"
        "movq %%r13, 24(%0)\n\t"
        "movq %%r14, 32(%0)\n\t"
        "movq %%r15, 40(%0)\n\t"
        "movq %%rsp, 48(%0)\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "movq %%rax, 56(%0)\n\t"
        "1:\n\t"
        : /* no output */
        : "r"(ctx)
        : "rax", "memory"
    );
    #elif defined(__i386__)
    asm volatile (
        "movl %%ebx, 0(%0)\n\t"
        "movl %%ebp, 4(%0)\n\t"
        "movl %%esi, 8(%0)\n\t"
        "movl %%edi, 12(%0)\n\t"
        "movl %%esp, 16(%0)\n\t"
        "leal 1f(%%eax), %%eax\n\t"
        "movl %%eax, 20(%0)\n\t"
        "1:\n\t"
        : /* no output */
        : "r"(ctx)
        : "eax", "memory"
    );
    #elif defined(__aarch64__)
    asm volatile (
        "stp x19, x20, [%0, #0]\n\t"
        "stp x21, x22, [%0, #16]\n\t"
        "stp x23, x24, [%0, #32]\n\t"
        "stp x25, x26, [%0, #48]\n\t"
        "stp x27, x28, [%0, #64]\n\t"
        "str x29, [%0, #80]\n\t"
        "mov x1, sp\n\t"
        "str x1, [%0, #88]\n\t"
        "str x30, [%0, #96]\n\t"
        "adr x1, 1f\n\t"
        "str x1, [%0, #104]\n\t"
        "1:\n\t"
        : /* no output */
        : "r"(ctx)
        : "x1", "memory"
    );
    #elif defined(__arm__)
    asm volatile (
        "stmia %0, {r4-r11}\n\t"
        "str sp, [%0, #32]\n\t"
        "str lr, [%0, #36]\n\t"
        "adr r0, 1f\n\t"
        "str r0, [%0, #40]\n\t"
        "1:\n\t"
        : /* no output */
        : "r"(ctx)
        : "r0", "memory"
    );
    #else
    if (ctx->is_main) {
        sigsetjmp(ctx->env, 0);
    }
    #endif
}

/* Context restoring */
static inline void ol_ctx_restore(ol_gt_ctx_t *ctx) __attribute__((noreturn));
static inline void ol_ctx_restore(ol_gt_ctx_t *ctx) {
    #if defined(__x86_64__)
    asm volatile (
        "movq 0(%0), %%rbx\n\t"
        "movq 8(%0), %%rbp\n\t"
        "movq 16(%0), %%r12\n\t"
        "movq 24(%0), %%r13\n\t"
        "movq 32(%0), %%r14\n\t"
        "movq 40(%0), %%r15\n\t"
        "movq 48(%0), %%rsp\n\t"
        "movq 56(%0), %%rax\n\t"
        "jmp *%%rax\n\t"
        : /* no output */
        : "r"(ctx)
        : "rax", "memory"
    );
    #elif defined(__i386__)
    asm volatile (
        "movl 0(%0), %%ebx\n\t"
        "movl 4(%0), %%ebp\n\t"
        "movl 8(%0), %%esi\n\t"
        "movl 12(%0), %%edi\n\t"
        "movl 16(%0), %%esp\n\t"
        "movl 20(%0), %%eax\n\t"
        "jmp *%%eax\n\t"
        : /* no output */
        : "r"(ctx)
        : "eax", "memory"
    );
    #elif defined(__aarch64__)
    asm volatile (
        "ldp x19, x20, [%0, #0]\n\t"
        "ldp x21, x22, [%0, #16]\n\t"
        "ldp x23, x24, [%0, #32]\n\t"
        "ldp x25, x26, [%0, #48]\n\t"
        "ldp x27, x28, [%0, #64]\n\t"
        "ldr x29, [%0, #80]\n\t"
        "ldr x1, [%0, #88]\n\t"
        "mov sp, x1\n\t"
        "ldr x30, [%0, #96]\n\t"
        "ldr x1, [%0, #104]\n\t"
        "br x1\n\t"
        : /* no output */
        : "r"(ctx)
        : "x1", "memory"
    );
    #elif defined(__arm__)
    asm volatile (
        "ldmia %0, {r4-r11}\n\t"
        "ldr sp, [%0, #32]\n\t"
        "ldr lr, [%0, #36]\n\t"
        "ldr r0, [%0, #40]\n\t"
        "bx r0\n\t"
        : /* no output */
        : "r"(ctx)
        : "r0", "memory"
    );
    #else
    siglongjmp(ctx->env, 1);
    #endif

    __builtin_unreachable();
}

static void ol_ctx_make(ol_gt_ctx_t *ctx, void (*fn)(void*), void *arg,
                        void *stack_base, size_t stack_size) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->stack = stack_base;
    ctx->stack_size = stack_size;
    ctx->is_main = 0;

    void *stack_top = (char*)stack_base + stack_size;

    /* align stack به 16 بایت */
    stack_top = (void*)((uintptr_t)stack_top & ~15);

    #if defined(__x86_64__)
    ctx->rsp = (char*)stack_top - 8;
    void **stack_slot = (void**)ctx->rsp;
    *stack_slot = (void*)fn;
    ctx->rip = (void*)ol_gt_trampoline;
    ctx->rbx = arg;

    #elif defined(__i386__)
    ctx->esp = (char*)stack_top - 4;
    void **stack_slot = (void**)ctx->esp;
    *stack_slot = (void*)fn;
    ctx->eip = (void*)ol_gt_trampoline;
    ctx->ebx = arg;

    #elif defined(__aarch64__)
    ctx->sp = stack_top;
    ctx->lr = (void*)fn;
    ctx->pc = (void*)ol_gt_trampoline;
    ctx->x19 = arg;  /* Save arg in x19 */

    #elif defined(__arm__)
    ctx->sp = stack_top;
    ctx->lr = (void*)fn;
    ctx->pc = (void*)ol_gt_trampoline;
    ctx->r4 = arg;  /* Save arg in r4 */

    #else
    /* Fallback: استفاده از sigsetjmp */
    ctx->stack = stack_base;
    ctx->stack_size = stack_size;
    ctx->is_main = 0;

    sigsetjmp(ctx->env, 0);
    #endif
}

/* Utility: enqueue a GT in ready list */
static void enqueue_ready(ol_gt_t *gt) {
    gt->next = NULL;
    if (!g_sched.ready_tail) {
        g_sched.ready_head = g_sched.ready_tail = gt;
    } else {
        g_sched.ready_tail->next = gt;
        g_sched.ready_tail = gt;
    }
}

/* Utility: pop next ready GT */
static ol_gt_t* dequeue_ready(void) {
    ol_gt_t *gt = g_sched.ready_head;
    if (gt) {
        g_sched.ready_head = gt->next;
        if (!g_sched.ready_head) g_sched.ready_tail = NULL;
        gt->next = NULL;
    }
    return gt;
}

/* Trampoline that runs in the GT context */
static void ol_gt_trampoline(uintptr_t arg_low, uintptr_t arg_high) {
    uintptr_t ptr = (arg_high << 32) | arg_low;
    ol_gt_t *gt = (ol_gt_t*)ptr;

    g_sched.current = gt;
    gt->state = OL_GT_RUNNING;

    if (__atomic_load_n(&gt->cancel_flag, __ATOMIC_RELAXED)) {
        gt->state = OL_GT_CANCELED;
        g_sched.current = NULL;
        ol_ctx_restore(&g_sched.sched_ctx);
        return;
    }

    gt->entry(gt->arg);

    gt->state = OL_GT_DONE;
    g_sched.current = NULL;

    ol_ctx_restore(&g_sched.sched_ctx);
}

static void ol_ctx_make_with_arg(ol_gt_ctx_t *ctx, void (*fn)(uintptr_t, uintptr_t),
    uintptr_t arg1, uintptr_t arg2,
    void *stack_base, size_t stack_size) {
    ol_ctx_make(ctx, (void(*)(void*))fn, (void*)((arg2 << 32) | arg1),
                stack_base, stack_size);
}

int ol_gt_scheduler_init(void) {
    if (g_sched.initialized) return 0;
    /* Initialize scheduler context to current */
    if (getcontext(&g_sched.sched_ctx) != 0) return -1;
    g_sched.ready_head = g_sched.ready_tail = NULL;
    g_sched.current = NULL;
    g_sched.default_stack = 256 * 1024;
    g_sched.initialized = true;
    return 0;
}

void ol_gt_scheduler_shutdown(void) {
    if (!g_sched.initialized) return;
    /* Mark residual ready threads canceled; they will clean up when scheduled */
    ol_gt_t *gt = g_sched.ready_head;
    while (gt) {
        ol_gt_t *nx = gt->next;
        __atomic_store_n(&gt->cancel_flag, 1, __ATOMIC_RELAXED);
        gt = nx;
    }
    g_sched.ready_head = g_sched.ready_tail = NULL;
    g_sched.current = NULL;
    g_sched.initialized = false;
}

ol_gt_t* ol_gt_spawn(ol_gt_entry_fn entry, void *arg, size_t stack_size) {
    if (!g_sched.initialized || !entry) return NULL;

    ol_gt_t *gt = (ol_gt_t*)calloc(1, sizeof(ol_gt_t));
    if (!gt) return NULL;

    gt->state = OL_GT_NEW;
    gt->arg = arg;
    gt->entry = entry;
    gt->joined = false;
    gt->cancel_flag = 0;
    gt->next = NULL;

    /* Allocate stack */
    size_t sz = (stack_size && stack_size >= (64 * 1024)) ? stack_size : g_sched.default_stack;
    gt->stack = malloc(sz);
    if (!gt->stack) { free(gt); return NULL; }
    gt->stack_size = sz;

    /* Prepare context */
    if (getcontext(&gt->ctx) != 0) {
        free(gt->stack);
        free(gt);
        return NULL;
    }
    gt->ctx.uc_stack.ss_sp = gt->stack;
    gt->ctx.uc_stack.ss_size = gt->stack_size;
    gt->ctx.uc_link = &g_sched.sched_ctx;

    /* Pass pointer via two 32-bit args to makecontext (portable trick) */
    uintptr_t ptr = (uintptr_t)gt;
    makecontext(&gt->ctx, (void (*)())ol_gt_trampoline, 2, (uint32_t)(ptr & 0xFFFFFFFFu), (uint32_t)(ptr >> 32));

    gt->state = OL_GT_READY;
    enqueue_ready(gt);
    return gt;
}

/* Run next ready task cooperatively */
static int ol_run_next(void) {
    ol_gt_t *gt = dequeue_ready();
    if (!gt) return 0;
    /* Switch from scheduler context to GT context */
    if (swapcontext(&g_sched.sched_ctx, &gt->ctx) != 0) {
        return -1;
    }
    /* After GT returns control to scheduler: if still runnable and not done/canceled, re-enqueue */
    if (gt->state == OL_GT_RUNNING) {
        gt->state = OL_GT_READY;
    }
    if (gt->state == OL_GT_READY) {
        enqueue_ready(gt);
    }
    return 1;
}

int ol_gt_resume(ol_gt_t *gt) {
    if (!g_sched.initialized || !gt) return -1;
    if (gt->state == OL_GT_DONE || gt->state == OL_GT_CANCELED) return -1;

    if (gt->state == OL_GT_READY || gt->state == OL_GT_NEW) {
        /* Cooperative scheduler: explicitly run this GT now by swapping to its context */
        if (swapcontext(&g_sched.sched_ctx, &gt->ctx) != 0) return -1;
        if (gt->state == OL_GT_RUNNING) gt->state = OL_GT_READY;
        return 0;
    }

    /* If running, cannot resume; otherwise try run next ready */
    return (ol_run_next() >= 0) ? 0 : -1;
}

void ol_gt_yield(void) {
    if (!g_sched.initialized) return;
    ol_gt_t *cur = g_sched.current;
    if (!cur) return;
    cur->state = OL_GT_READY;
    /* Switch back to scheduler */
    swapcontext(&cur->ctx, &g_sched.sched_ctx);
}

int ol_gt_join(ol_gt_t *gt) {
    if (!g_sched.initialized || !gt) return -1;
    while (gt->state != OL_GT_DONE && gt->state != OL_GT_CANCELED) {
        if (!ol_run_next()) {
            /* No ready tasks left; deadlock in cooperative mode */
            return -1;
        }
    }
    gt->joined = true;
    return 0;
}

int ol_gt_cancel(ol_gt_t *gt) {
    if (!gt) return -1;
    if (gt->state == OL_GT_DONE || gt->state == OL_GT_CANCELED) return -1;
    __atomic_store_n(&gt->cancel_flag, 1, __ATOMIC_RELAXED);
    /* Thread observes cancellation at next yield or at start trampoline */
    return 0;
}

bool ol_gt_is_alive(const ol_gt_t *gt) {
    if (!gt) return false;
    return gt->state != OL_GT_DONE && gt->state != OL_GT_CANCELED;
}

bool ol_gt_is_canceled(const ol_gt_t *gt) {
    if (!gt) return false;
    return __atomic_load_n(&gt->cancel_flag, __ATOMIC_RELAXED) != 0;
}

ol_gt_t* ol_gt_current(void) {
    return g_sched.current;
}

/* POSIX: destroy — upgraded to ensure the GT is finished before freeing stack */
void ol_gt_destroy(ol_gt_t *gt) {
    if (!gt) return;
    /* Must be done or canceled */
    if (gt->state != OL_GT_DONE && gt->state != OL_GT_CANCELED) {
        /* best-effort cancel */
        __atomic_store_n(&gt->cancel_flag, 1, __ATOMIC_RELAXED);
        /* cooperative join attempt to avoid freeing a running stack */
        (void)ol_gt_join(gt);
    }
    if (gt->stack) {
        free(gt->stack);
        gt->stack = NULL;
    }
    free(gt);
}

/* --------------------------- End POSIX (ucontext) --------------------------- */
#endif
