/* ol_green_threads.c - Cross-platform Green Threads Implementation */

#include "ol_green_threads.h"
#include <stdlib.h>
#include <string.h>

/* ------------------- Platform Detection ------------------- */
#if defined(_WIN32)
    #define OL_PLATFORM_WINDOWS 1
    #include <windows.h>
#elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
      defined(__OpenBSD__) || defined(__NetBSD__)
    #define OL_PLATFORM_POSIX 1
    
    /* POSIX Headers */
    #include <unistd.h>
    #include <sys/mman.h>
    #include <signal.h>
    
    /* For assembly context switching */
    #if defined(__x86_64__) || defined(__i386__) || \
        defined(__aarch64__) || defined(__arm__)
        #define OL_USE_ASM_CONTEXT 1
    #else
        /* Fallback to ucontext for unknown architectures */
        #define OL_USE_UCONTEXT 1
        #include <ucontext.h>
    #endif
#else
    #error "Unsupported platform"
#endif

/* ------------------- Common Structures ------------------- */
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
    
    /* Platform-specific context */
#if OL_PLATFORM_WINDOWS
    LPVOID fiber;
#elif OL_USE_UCONTEXT
    ucontext_t ctx;
#else
    ol_gt_ctx_t ctx;
#endif
    
    /* Stack management */
    void *stack;
    size_t stack_size;
    
    /* Cooperative join flag */
    bool joined;
    
    /* Cancellation flag */
    volatile long cancel_flag;
    
    /* Linking in scheduler list */
    struct ol_gt *next;
} ol_gt_t;

/* Scheduler context */
typedef struct {
    /* Platform-specific scheduler context */
#if OL_PLATFORM_WINDOWS
    LPVOID scheduler_fiber;
#elif OL_USE_UCONTEXT
    ucontext_t sched_ctx;
#else
    ol_gt_ctx_t sched_ctx;
#endif
    
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

/* Thread-local scheduler instance */
#if OL_PLATFORM_WINDOWS
    static __declspec(thread) ol_gt_sched_t g_sched = {0};
#else
    static __thread ol_gt_sched_t g_sched = {0};
#endif

/* ------------------- Utility Functions ------------------- */
static void enqueue_ready(ol_gt_t *gt) {
    gt->next = NULL;
    if (!g_sched.ready_tail) {
        g_sched.ready_head = g_sched.ready_tail = gt;
    } else {
        g_sched.ready_tail->next = gt;
        g_sched.ready_tail = gt;
    }
}

static ol_gt_t* dequeue_ready(void) {
    ol_gt_t *gt = g_sched.ready_head;
    if (gt) {
        g_sched.ready_head = gt->next;
        if (!g_sched.ready_head) {
            g_sched.ready_tail = NULL;
        }
        gt->next = NULL;
    }
    return gt;
}

static void remove_from_ready(ol_gt_t *gt) {
    if (!g_sched.ready_head) return;
    
    if (g_sched.ready_head == gt) {
        g_sched.ready_head = gt->next;
        if (!g_sched.ready_head) {
            g_sched.ready_tail = NULL;
        }
        gt->next = NULL;
        return;
    }
    
    ol_gt_t *prev = g_sched.ready_head;
    while (prev->next && prev->next != gt) {
        prev = prev->next;
    }
    
    if (prev->next == gt) {
        prev->next = gt->next;
        if (g_sched.ready_tail == gt) {
            g_sched.ready_tail = prev;
        }
        gt->next = NULL;
    }
}

/* ------------------- Platform-Specific Implementations ------------------- */

#if OL_PLATFORM_WINDOWS
/* ============= Windows Implementation (Fibers) ============= */

static VOID CALLBACK ol_fiber_start(LPVOID param) {
    ol_gt_t *gt = (ol_gt_t*)param;
    g_sched.current = gt;
    gt->state = OL_GT_RUNNING;
    
    /* Check cancellation */
    if (InterlockedCompareExchange(&gt->cancel_flag, 0, 0) != 0) {
        gt->state = OL_GT_CANCELED;
        g_sched.current = NULL;
        SwitchToFiber(g_sched.scheduler_fiber);
        return;
    }
    
    /* Run user function */
    gt->entry(gt->arg);
    
    gt->state = OL_GT_DONE;
    g_sched.current = NULL;
    SwitchToFiber(g_sched.scheduler_fiber);
}

int ol_gt_scheduler_init(void) {
    if (g_sched.initialized) return 0;
    
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
    
    /* Mark all ready threads as canceled */
    ol_gt_t *gt = g_sched.ready_head;
    while (gt) {
        ol_gt_t *nx = gt->next;
        InterlockedExchange(&gt->cancel_flag, 1);
        gt = nx;
    }
    
    g_sched.ready_head = g_sched.ready_tail = NULL;
    g_sched.current = NULL;
    g_sched.initialized = false;
    
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
    gt->stack = NULL;
    gt->stack_size = 0;
    
    SIZE_T sz = (stack_size && stack_size >= (64 * 1024)) ? 
                (SIZE_T)stack_size : (SIZE_T)g_sched.default_stack;
    
    gt->fiber = CreateFiber(sz, ol_fiber_start, gt);
    if (!gt->fiber) {
        free(gt);
        return NULL;
    }
    
    gt->state = OL_GT_READY;
    enqueue_ready(gt);
    return gt;
}

static int ol_run_next(void) {
    ol_gt_t *gt = dequeue_ready();
    if (!gt) return 0;
    
    SwitchToFiber(gt->fiber);
    
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
    
    if (gt->state == OL_GT_READY) {
        remove_from_ready(gt);
        SwitchToFiber(gt->fiber);
        if (gt->state == OL_GT_RUNNING) {
            gt->state = OL_GT_READY;
        }
        return 0;
    }
    
    return (ol_run_next() >= 0) ? 0 : -1;
}

void ol_gt_yield(void) {
    if (!g_sched.initialized) return;
    ol_gt_t *cur = g_sched.current;
    if (!cur) return;
    
    cur->state = OL_GT_READY;
    SwitchToFiber(g_sched.scheduler_fiber);
}

void ol_gt_destroy(ol_gt_t *gt) {
    if (!gt) return;
    
    if (gt->state != OL_GT_DONE && gt->state != OL_GT_CANCELED) {
        InterlockedExchange(&gt->cancel_flag, 1);
        (void)ol_gt_join(gt);
    }
    
    if (gt->fiber) {
        DeleteFiber(gt->fiber);
        gt->fiber = NULL;
    }
    
    free(gt);
}

#elif OL_PLATFORM_POSIX
/* ============= POSIX Implementation ============= */

/* Get page size in a portable way */
static size_t ol_get_page_size(void) {
    static size_t page_size = 0;
    
    if (page_size == 0) {
        #if defined(_SC_PAGESIZE)
            long ps = sysconf(_SC_PAGESIZE);
            page_size = (ps > 0) ? (size_t)ps : 4096;
        #elif defined(_SC_PAGE_SIZE)
            long ps = sysconf(_SC_PAGE_SIZE);
            page_size = (ps > 0) ? (size_t)ps : 4096;
        #else
            page_size = 4096; /* Common default */
        #endif
    }
    
    return page_size;
}

/* Trampoline function */
static void ol_gt_trampoline(uintptr_t arg_low, uintptr_t arg_high);

#if OL_USE_ASM_CONTEXT
/* ============= Assembly Context Switching ============= */

/* Save current context */
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
    #endif
}

/* Restore context */
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
    #endif
    
    __builtin_unreachable();
}

/* Create context */
static void ol_ctx_make(ol_gt_ctx_t *ctx, void (*fn)(void*), void *arg,
                        void *stack_base, size_t stack_size) {
    memset(ctx, 0, sizeof(*ctx));
    
    void *stack_top = (char*)stack_base + stack_size;
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
    ctx->x19 = arg;
    #elif defined(__arm__)
    ctx->sp = stack_top;
    ctx->lr = (void*)fn;
    ctx->pc = (void*)ol_gt_trampoline;
    ctx->r4 = arg;
    #endif
}

static void ol_ctx_make_with_arg(ol_gt_ctx_t *ctx, 
                                 void (*fn)(uintptr_t, uintptr_t),
                                 uintptr_t arg1, uintptr_t arg2,
                                 void *stack_base, size_t stack_size) {
    ol_ctx_make(ctx, (void(*)(void*))fn, 
                (void*)((arg2 << 32) | arg1),
                stack_base, stack_size);
}

#elif OL_USE_UCONTEXT
/* ============= ucontext Implementation ============= */

static void ol_gt_trampoline_wrapper(uintptr_t arg1, uintptr_t arg2) {
    ol_gt_trampoline(arg1, arg2);
}

static void ol_ctx_save(ucontext_t *ctx) {
    getcontext(ctx);
}

static void ol_ctx_restore(ucontext_t *ctx) {
    setcontext(ctx);
}

static void ol_ctx_make_with_arg(ucontext_t *ctx, 
                                 void (*fn)(uintptr_t, uintptr_t),
                                 uintptr_t arg1, uintptr_t arg2,
                                 void *stack_base, size_t stack_size) {
    getcontext(ctx);
    ctx->uc_stack.ss_sp = stack_base;
    ctx->uc_stack.ss_size = stack_size;
    ctx->uc_link = &g_sched.sched_ctx;
    
    uintptr_t ptr = (arg2 << 32) | arg1;
    makecontext(ctx, (void(*)(void))ol_gt_trampoline_wrapper, 2, arg1, arg2);
}

#endif /* OL_USE_ASM_CONTEXT vs OL_USE_UCONTEXT */

/* ============= Common POSIX Implementation ============= */

int ol_gt_scheduler_init(void) {
    if (g_sched.initialized) return 0;
    
#if OL_USE_ASM_CONTEXT
    memset(&g_sched.sched_ctx, 0, sizeof(g_sched.sched_ctx));
    ol_ctx_save(&g_sched.sched_ctx);
#elif OL_USE_UCONTEXT
    getcontext(&g_sched.sched_ctx);
#endif
    
    g_sched.ready_head = g_sched.ready_tail = NULL;
    g_sched.current = NULL;
    g_sched.default_stack = 256 * 1024;
    g_sched.initialized = true;
    
    return 0;
}

void ol_gt_scheduler_shutdown(void) {
    if (!g_sched.initialized) return;
    
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

/* Trampoline */
static void ol_gt_trampoline(uintptr_t arg_low, uintptr_t arg_high) {
    uintptr_t ptr = (arg_high << 32) | arg_low;
    ol_gt_t *gt = (ol_gt_t*)ptr;
    
    g_sched.current = gt;
    gt->state = OL_GT_RUNNING;
    
    if (__atomic_load_n(&gt->cancel_flag, __ATOMIC_RELAXED)) {
        gt->state = OL_GT_CANCELED;
        g_sched.current = NULL;
#if OL_USE_ASM_CONTEXT
        ol_ctx_restore(&g_sched.sched_ctx);
#elif OL_USE_UCONTEXT
        setcontext(&g_sched.sched_ctx);
#endif
        return;
    }
    
    gt->entry(gt->arg);
    
    gt->state = OL_GT_DONE;
    g_sched.current = NULL;
    
#if OL_USE_ASM_CONTEXT
    ol_ctx_restore(&g_sched.sched_ctx);
#elif OL_USE_UCONTEXT
    setcontext(&g_sched.sched_ctx);
#endif
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
    
    size_t sz = (stack_size && stack_size >= (64 * 1024)) ? 
                stack_size : g_sched.default_stack;
    sz = (sz + 15) & ~15;
    
    size_t page_size = ol_get_page_size();
    size_t total_size = sz + 2 * page_size;
    
    void *stack_area = mmap(NULL, total_size, 
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack_area == MAP_FAILED) {
        free(gt);
        return NULL;
    }
    
    mprotect(stack_area, page_size, PROT_NONE);
    mprotect((char*)stack_area + total_size - page_size, page_size, PROT_NONE);
    
    gt->stack = (char*)stack_area + page_size;
    gt->stack_size = sz;
    
    uintptr_t ptr = (uintptr_t)gt;
    
#if OL_USE_ASM_CONTEXT
    ol_ctx_make_with_arg(&gt->ctx, ol_gt_trampoline,
                         (uintptr_t)(ptr & 0xFFFFFFFFu),
                         (uintptr_t)(ptr >> 32),
                         gt->stack, gt->stack_size);
#elif OL_USE_UCONTEXT
    ol_ctx_make_with_arg(&gt->ctx, ol_gt_trampoline,
                         (uintptr_t)(ptr & 0xFFFFFFFFu),
                         (uintptr_t)(ptr >> 32),
                         gt->stack, gt->stack_size);
#endif
    
    gt->state = OL_GT_READY;
    enqueue_ready(gt);
    return gt;
}

static int ol_run_next(void) {
    ol_gt_t *gt = dequeue_ready();
    if (!gt) return 0;
    
#if OL_USE_ASM_CONTEXT
    ol_ctx_save(&g_sched.sched_ctx);
    g_sched.current = gt;
    ol_ctx_restore(&gt->ctx);
#elif OL_USE_UCONTEXT
    swapcontext(&g_sched.sched_ctx, &gt->ctx);
#endif
    
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
        remove_from_ready(gt);
        
#if OL_USE_ASM_CONTEXT
        ol_ctx_save(&g_sched.sched_ctx);
        g_sched.current = gt;
        ol_ctx_restore(&gt->ctx);
#elif OL_USE_UCONTEXT
        swapcontext(&g_sched.sched_ctx, &gt->ctx);
#endif
        
        if (gt->state == OL_GT_RUNNING) {
            gt->state = OL_GT_READY;
        }
        return 0;
    }
    
    return (ol_run_next() >= 0) ? 0 : -1;
}

void ol_gt_yield(void) {
    if (!g_sched.initialized) return;
    ol_gt_t *cur = g_sched.current;
    if (!cur) return;
    
    cur->state = OL_GT_READY;
    
#if OL_USE_ASM_CONTEXT
    ol_ctx_save(&cur->ctx);
    ol_ctx_restore(&g_sched.sched_ctx);
#elif OL_USE_UCONTEXT
    swapcontext(&cur->ctx, &g_sched.sched_ctx);
#endif
}

int ol_gt_join(ol_gt_t *gt) {
    if (!g_sched.initialized || !gt) return -1;
    
    while (gt->state != OL_GT_DONE && gt->state != OL_GT_CANCELED) {
        if (!ol_run_next()) {
            return -1;
        }
    }
    
    gt->joined = true;
    return 0;
}

void ol_gt_destroy(ol_gt_t *gt) {
    if (!gt) return;
    
    if (gt->state != OL_GT_DONE && gt->state != OL_GT_CANCELED) {
        __atomic_store_n(&gt->cancel_flag, 1, __ATOMIC_RELAXED);
        (void)ol_gt_join(gt);
    }
    
    if (gt->stack) {
        size_t page_size = ol_get_page_size();
        void *stack_area = (char*)gt->stack - page_size;
        size_t total_size = gt->stack_size + 2 * page_size;
        munmap(stack_area, total_size);
        gt->stack = NULL;
    }
    
    free(gt);
}

#endif /* OL_PLATFORM_WINDOWS vs OL_PLATFORM_POSIX */

/* ------------------- Common API Functions ------------------- */

int ol_gt_cancel(ol_gt_t *gt) {
    if (!gt) return -1;
    if (gt->state == OL_GT_DONE || gt->state == OL_GT_CANCELED) return -1;
    
#if OL_PLATFORM_WINDOWS
    InterlockedExchange(&gt->cancel_flag, 1);
#else
    __atomic_store_n(&gt->cancel_flag, 1, __ATOMIC_RELAXED);
#endif
    
    return 0;
}

bool ol_gt_is_alive(const ol_gt_t *gt) {
    if (!gt) return false;
    return gt->state != OL_GT_DONE && gt->state != OL_GT_CANCELED;
}

bool ol_gt_is_canceled(const ol_gt_t *gt) {
    if (!gt) return false;
    
#if OL_PLATFORM_WINDOWS
    return InterlockedCompareExchange((LONG*)&gt->cancel_flag, 0, 0) != 0;
#else
    return __atomic_load_n(&gt->cancel_flag, __ATOMIC_RELAXED) != 0;
#endif
}

ol_gt_t* ol_gt_current(void) {
    return g_sched.current;
}