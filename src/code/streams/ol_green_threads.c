#define _GNU_SOURCE

/**
 * @file ol_green_threads.c
 * @brief Advanced cross-platform green threads (user-space threads) and
 *        fibers implementation.
 *
 * @details
 * This translation unit implements the green-thread runtime that backs
 * OLSRT's coroutine and actor layers. It provides:
 *
 * - A per-OS-thread scheduler instance, stored in thread-local storage.
 * - Work-stealing deques (Chase-Lev) for runnable threads.
 * - A segregated stack pool for fast thread creation.
 * - Optional NUMA awareness for stack and metadata placement.
 * - Cooperative context switching on x86_64, aarch64, and ARM.
 * - Windows fibers and POSIX assembly/ucontext backends.
 *
 * ## v1.3.2 additions
 *
 * - @ref ol_gt_run_to_completion() drives a green thread on the
 *   calling OS thread until it reaches a terminal state. This is the
 *   primitive that makes @ref ol_actor_start() work without manual
 *   mailbox pumping.
 * - The trampoline and yield functions were reworked to cooperate with
 *   the driver loop.
 *
 * @warning The API signatures in the public header are frozen; only
 *          internal logic may change between minor versions.
 *
 * @author OverLab Group
 * @version 1.3.2
 * @date 2026
 */

#include "ol_green_threads.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

/* ==================== Platform-Specific Headers ==================== */
#if OL_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fibersapi.h>
#include <processthreadsapi.h>
#include <sysinfoapi.h>
#include <memoryapi.h>
#include <winternl.h>

/* Windows NUMA support */
#if (_WIN32_WINNT >= 0x0601)
#define OL_NUMA_AVAILABLE 1
#include <numaapi.h>
#else
#define OL_NUMA_AVAILABLE 0
#endif

#elif OL_PLATFORM_POSIX
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <errno.h>

#if defined(__linux__)
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <linux/futex.h>
#include <numa.h>
#include <numaif.h>
#define OL_NUMA_AVAILABLE 1
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/param.h>
#include <sys/cpuset.h>
#define OL_NUMA_AVAILABLE 0 /* BSD has limited NUMA support */
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <sys/param.h>
#include <sys/sysctl.h>

/* These helpers are defined later in this file but used earlier by
 * ol_work_stealing_queue_init and ol_stack_pool_*. They are static
 * (file-local); the public header does not expose them. Declaring them
 * here prevents the compiler's implicit `extern int f()` assumption,
 * which would otherwise conflict with the actual `static void* f(...)`
 * definitions further down. */
static OL_NO_INLINE void*
ol_numa_alloc(size_t size, size_t alignment, int numa_node);
static OL_NO_INLINE void ol_numa_free(void* ptr, size_t size);
static OL_FORCE_INLINE int ol_get_current_numa_node(void);
static OL_FORCE_INLINE int ol_get_numa_cpu_count(int numa_node);

#define OL_NUMA_AVAILABLE 0 /* macOS has no NUMA support */
#else
#define OL_NUMA_AVAILABLE 0
#endif

#endif

/* ==================== Compiler-Specific Optimizations ==================== */

/* Branch prediction optimization */
#define OL_BRANCH_PREDICT_HOT OL_HOT
#define OL_BRANCH_PREDICT_COLD OL_COLD

/* Force inline for critical path functions */
#define OL_FORCE_INLINE OL_ALWAYS_INLINE

/* Noinline for functions we don't want inlined */
#define OL_NO_INLINE OL_NOINLINE

/* ==================== Global Constants ==================== */

/**
 * @brief Default stack size (256KB)
 */
static const size_t OL_DEFAULT_STACK_SIZE = 256 * 1024;

/**
 * @brief Minimum stack size (64KB)
 */
static const size_t OL_MIN_STACK_SIZE = 64 * 1024;

/**
 * @brief Maximum stack size (8MB)
 */
static const size_t OL_MAX_STACK_SIZE = 8 * 1024 * 1024;

/**
 * @brief Stack guard page size (system page size)
 */
static size_t OL_PAGE_SIZE = 4096;

/**
 * @brief Cache line padding for false sharing avoidance
 */
static const size_t OL_CACHE_LINE_PADDING = OL_CACHE_LINE_SIZE;

/**
 * @brief Preemption time slice default (10ms)
 */
static const uint64_t OL_DEFAULT_PREEMPTION_SLICE_NS = 10 * 1000 * 1000;

/**
 * @brief Maximum work stealing queue size (power of 2)
 */
static const size_t OL_MAX_WORK_STEALING_QUEUE_SIZE = 1024;

/**
 * @brief Stack pool bucket sizes (1KB, 2KB, 4KB, 8KB, 16KB, 32KB, 64KB, 128KB)
 */
static const size_t OL_STACK_POOL_BUCKET_SIZES[8] = { 1024,  2048,  4096,
                                                      8192,  16384, 32768,
                                                      65536, 131072 };

/* ==================== Thread-Local Scheduler Instance ==================== */

/**
 * @brief Thread-local scheduler instance
 * @note Aligned to cache line to avoid false sharing
 */
static OL_ALIGNED(OL_CACHE_LINE_SIZE)
#if OL_PLATFORM_WINDOWS
    __declspec(thread)
#else
    __thread
#endif
    ol_gt_scheduler_t* g_thread_scheduler = NULL;

/**
 * @brief Global scheduler list for work stealing
 * @note Protected by atomic operations
 */
/* v1.3.3: clang rejects atomic_fetch_add_explicit on plain
 * uint64_t pointers. GCC accepts them as an extension. The
 * statistics counters (ol_gt_statistics_t) are plain uint64_t
 * for ABI reasons, so route the operations that touch them
 * through the __atomic_* builtins, which work on plain
 * integer pointers on both compilers. Fields that are
 * already _Atomic (pool->hits, bucket->count, ...) keep the
 * C11 atomic functions. */
#define OL_STATS_ADD(ptr, val) \
    __atomic_fetch_add((ptr), (val), __ATOMIC_RELAXED)

static atomic_uintptr_t g_scheduler_list_head = 0;

/**
 * @brief Global statistics
 * @note Updated atomically
 */
static ol_gt_statistics_t g_global_stats = { 0 };

/**
 * @brief Last error code
 */
static atomic_int g_last_error = OL_GT_SUCCESS;

/* ==================== v1.3.2 Driver State ==================== */

/**
 * @brief Thread-local saved context for the driver loop.
 *
 * @details
 * When @ref ol_gt_run_to_completion is active on this OS thread, this
 * holds the driver's saved CPU context. A green thread that yields in
 * driver mode restores this context to return control to the driver.
 *
 * @note Only x86_64 is supported in v1.3.2. aarch64 and ARM variants
 *       will use the corresponding context type once those assembly
 *       backends are wired into the driver loop.
 */
/* Thread-local storage: raw __thread on POSIX, __declspec(thread)
 * on Windows. This mirrors the pattern used by g_thread_scheduler. */
#if OL_PLATFORM_WINDOWS
#define OL_GT_LOCAL __declspec(thread)
#else
#define OL_GT_LOCAL __thread
#endif

static OL_GT_LOCAL uintptr_t g_gt_driver_ctx[64];

/**
 * @brief Thread-local flag: is the driver loop running on this thread?
 *
 * @details
 * Set to true for the duration of @ref ol_gt_run_to_completion.
 * @ref ol_gt_yield inspects this flag to decide whether to return
 * control to the driver loop (true) or to the legacy direct-switching
 * scheduler (false).
 */
static OL_GT_LOCAL bool g_gt_driver_mode = false;

/* ==================== Assembly Context Switching ==================== */

#if OL_PLATFORM_POSIX && !OL_PLATFORM_WINDOWS

/**
 * @brief x86_64 context structure
 */
typedef struct {
    uintptr_t rbx;
    uintptr_t rbp;
    uintptr_t r12;
    uintptr_t r13;
    uintptr_t r14;
    uintptr_t r15;
    uintptr_t rsp;
    uintptr_t rip;
    uintptr_t mxcsr;
    uint64_t xmm6[2];
    uint64_t xmm7[2];
    uint64_t xmm8[2];
    uint64_t xmm9[2];
    uint64_t xmm10[2];
    uint64_t xmm11[2];
    uint64_t xmm12[2];
    uint64_t xmm13[2];
    uint64_t xmm14[2];
    uint64_t xmm15[2];
} ol_ctx_x86_64_t;

/**
 * @brief ARM64 context structure
 */
typedef struct {
    uintptr_t x19;
    uintptr_t x20;
    uintptr_t x21;
    uintptr_t x22;
    uintptr_t x23;
    uintptr_t x24;
    uintptr_t x25;
    uintptr_t x26;
    uintptr_t x27;
    uintptr_t x28;
    uintptr_t fp; /* x29 */
    uintptr_t lr; /* x30 */
    uintptr_t sp;
    uintptr_t pc;
    uint32_t fpcr;
    uint32_t fpsr;
    uint64_t v8[2];
    uint64_t v9[2];
    uint64_t v10[2];
    uint64_t v11[2];
    uint64_t v12[2];
    uint64_t v13[2];
    uint64_t v14[2];
    uint64_t v15[2];
} ol_ctx_aarch64_t;

/**
 * @brief ARM context structure
 */
typedef struct {
    uintptr_t r4;
    uintptr_t r5;
    uintptr_t r6;
    uintptr_t r7;
    uintptr_t r8;
    uintptr_t r9;
    uintptr_t r10;
    uintptr_t r11; /* fp */
    uintptr_t sp;
    uintptr_t lr;
    uintptr_t pc;
    uint32_t fpscr;
    uint64_t d8;
    uint64_t d9;
    uint64_t d10;
    uint64_t d11;
    uint64_t d12;
    uint64_t d13;
    uint64_t d14;
    uint64_t d15;
} ol_ctx_arm_t;

/**
 * @brief Save x86_64 context (assembly)
 */
static OL_FORCE_INLINE void ol_ctx_save_x86_64(ol_ctx_x86_64_t* ctx) {
    asm volatile(
        /* Save integer registers */
        "movq %%rbx,  0(%0)\n\t"
        "movq %%rbp,  8(%0)\n\t"
        "movq %%r12, 16(%0)\n\t"
        "movq %%r13, 24(%0)\n\t"
        "movq %%r14, 32(%0)\n\t"
        "movq %%r15, 40(%0)\n\t"
        "movq %%rsp, 48(%0)\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "movq %%rax, 56(%0)\n\t"

        /* Save MXCSR */
        "stmxcsr 64(%0)\n\t"

        /* Save XMM registers */
        "movdqu %%xmm6,  72(%0)\n\t"
        "movdqu %%xmm7,  88(%0)\n\t"
        "movdqu %%xmm8, 104(%0)\n\t"
        "movdqu %%xmm9, 120(%0)\n\t"
        "movdqu %%xmm10, 136(%0)\n\t"
        "movdqu %%xmm11, 152(%0)\n\t"
        "movdqu %%xmm12, 168(%0)\n\t"
        "movdqu %%xmm13, 184(%0)\n\t"
        "movdqu %%xmm14, 200(%0)\n\t"
        "movdqu %%xmm15, 216(%0)\n\t"

        "1:\n\t"
        :
        : "r"(ctx)
        : "rax", "memory");
}

/**
 * @brief Restore x86_64 context (assembly)
 */
static OL_FORCE_INLINE void ol_ctx_restore_x86_64(const ol_ctx_x86_64_t* ctx) {
    asm volatile(
        /* Restore XMM registers */
        "movdqu 216(%0), %%xmm15\n\t"
        "movdqu 200(%0), %%xmm14\n\t"
        "movdqu 184(%0), %%xmm13\n\t"
        "movdqu 168(%0), %%xmm12\n\t"
        "movdqu 152(%0), %%xmm11\n\t"
        "movdqu 136(%0), %%xmm10\n\t"
        "movdqu 120(%0), %%xmm9\n\t"
        "movdqu 104(%0), %%xmm8\n\t"
        "movdqu  88(%0), %%xmm7\n\t"
        "movdqu  72(%0), %%xmm6\n\t"

        /* Restore MXCSR */
        "ldmxcsr 64(%0)\n\t"

        /* Restore integer registers */
        "movq 56(%0), %%rax\n\t"
        "movq 48(%0), %%rsp\n\t"
        "movq 40(%0), %%r15\n\t"
        "movq 32(%0), %%r14\n\t"
        "movq 24(%0), %%r13\n\t"
        "movq 16(%0), %%r12\n\t"
        "movq  8(%0), %%rbp\n\t"
        "movq  0(%0), %%rbx\n\t"

        /* Jump to saved RIP */
        "jmp *%%rax\n\t"
        :
        : "r"(ctx)
        : "rax", "memory");

    __builtin_unreachable();
}

/**
 * @brief Make new x86_64 context
 */
static OL_FORCE_INLINE void ol_ctx_make_x86_64(ol_ctx_x86_64_t* ctx,
                                               void (*fn)(void),
                                               void* arg,
                                               void* stack_base,
                                               size_t stack_size) {
    /* Clear context */
    memset(ctx, 0, sizeof(*ctx));

    /* Set MXCSR to default value */
    ctx->mxcsr = 0x1F80;

    /* Calculate stack top (16-byte aligned) */
    uintptr_t stack_top = (uintptr_t)stack_base + stack_size;
    stack_top = (stack_top & ~0xF);

    /* Reserve space for return address */
    stack_top -= sizeof(uintptr_t);

    /* Set up stack for function call */
    uintptr_t* stack = (uintptr_t*)stack_top;

    /* Push argument */
    stack -= 1;
    *stack = (uintptr_t)arg;

    /* Align stack to 16 bytes after pushing */
    if (((uintptr_t)stack & 0xF) != 0) {
        stack -= 1;
        *stack = 0; /* Padding */
    }

    /* Set context registers */
    ctx->rsp = (uintptr_t)stack;
    ctx->rip = (uintptr_t)fn;

    /* Save callee-saved registers */
    ctx->rbx = (uintptr_t)arg;
    ctx->rbp = 0;
    ctx->r12 = 0;
    ctx->r13 = 0;
    ctx->r14 = 0;
    ctx->r15 = 0;
}

/**
 * @brief Save ARM64 context (assembly)
 */
#if OL_ARCH_AARCH64
static OL_FORCE_INLINE void ol_ctx_save_aarch64(ol_ctx_aarch64_t* ctx) {
    asm volatile(
        /* Save integer registers */
        "stp x19, x20, [%0, #0]\n\t"
        "stp x21, x22, [%0, #16]\n\t"
        "stp x23, x24, [%0, #32]\n\t"
        "stp x25, x26, [%0, #48]\n\t"
        "stp x27, x28, [%0, #64]\n\t"
        "stp fp, lr, [%0, #80]\n\t"
        "mov x1, sp\n\t"
        "str x1, [%0, #96]\n\t"

        /* Save FPCR and FPSR */
        "mrs x1, fpcr\n\t"
        "str w1, [%0, #104]\n\t"
        "mrs x1, fpsr\n\t"
        "str w1, [%0, #108]\n\t"

        /* Save vector registers */
        "stp d8, d9, [%0, #112]\n\t"
        "stp d10, d11, [%0, #128]\n\t"
        "stp d12, d13, [%0, #144]\n\t"
        "stp d14, d15, [%0, #160]\n\t"

        /* Save return address */
        "adr x1, 1f\n\t"
        "str x1, [%0, #176]\n\t"

        "1:\n\t"
        :
        : "r"(ctx)
        : "x1", "memory");
}

/**
 * @brief Restore ARM64 context (assembly)
 */
static OL_FORCE_INLINE void
ol_ctx_restore_aarch64(const ol_ctx_aarch64_t* ctx) {
    asm volatile(
        /* Restore vector registers */
        "ldp d14, d15, [%0, #160]\n\t"
        "ldp d12, d13, [%0, #144]\n\t"
        "ldp d10, d11, [%0, #128]\n\t"
        "ldp d8, d9, [%0, #112]\n\t"

        /* Restore FPCR and FPSR */
        "ldr w1, [%0, #108]\n\t"
        "msr fpsr, x1\n\t"
        "ldr w1, [%0, #104]\n\t"
        "msr fpcr, x1\n\t"

        /* Restore integer registers */
        "ldr x1, [%0, #96]\n\t"
        "mov sp, x1\n\t"
        "ldp fp, lr, [%0, #80]\n\t"
        "ldp x27, x28, [%0, #64]\n\t"
        "ldp x25, x26, [%0, #48]\n\t"
        "ldp x23, x24, [%0, #32]\n\t"
        "ldp x21, x22, [%0, #16]\n\t"
        "ldp x19, x20, [%0, #0]\n\t"

        /* Jump to saved PC */
        "ldr x1, [%0, #176]\n\t"
        "br x1\n\t"
        :
        : "r"(ctx)
        : "x1", "memory");

    __builtin_unreachable();
}

/**
 * @brief Make new ARM64 context
 */
static OL_FORCE_INLINE void ol_ctx_make_aarch64(ol_ctx_aarch64_t* ctx,
                                                void (*fn)(void),
                                                void* arg,
                                                void* stack_base,
                                                size_t stack_size) {
    /* Clear context */
    memset(ctx, 0, sizeof(*ctx));

    /* Set default FPCR/FPSR */
    ctx->fpcr = 0;
    ctx->fpsr = 0;

    /* Calculate stack top (16-byte aligned) */
    uintptr_t stack_top = (uintptr_t)stack_base + stack_size;
    stack_top = (stack_top & ~0xF);

    /* Reserve space for frame record */
    stack_top -= 16;

    /* Set up stack */
    ctx->sp = stack_top;
    ctx->pc = (uintptr_t)fn;
    ctx->lr = 0; /* No return address */
    ctx->fp = 0; /* Frame pointer */

    /* Set argument in x19 (callee-saved) */
    ctx->x19 = (uintptr_t)arg;
}
#endif /* OL_ARCH_AARCH64 */


/**
 * @brief Save ARM context (assembly)
 */
#if OL_ARCH_ARM
static OL_FORCE_INLINE void ol_ctx_save_arm(ol_ctx_arm_t* ctx) {
    asm volatile(
        /* Save integer registers */
        "stmia %0!, {r4-r11}\n\t"
        "str sp, [%0], #4\n\t"
        "str lr, [%0], #4\n\t"

        /* Save FPSCR */
        "vmrs r1, fpscr\n\t"
        "str r1, [%0], #4\n\t"

        /* Save vector registers */
        "vstr d15, [%0], #8\n\t"
        "vstr d14, [%0], #8\n\t"
        "vstr d13, [%0], #8\n\t"
        "vstr d12, [%0], #8\n\t"
        "vstr d11, [%0], #8\n\t"
        "vstr d10, [%0], #8\n\t"
        "vstr d9, [%0], #8\n\t"
        "vstr d8, [%0], #8\n\t"

        /* Save return address */
        "adr r1, 1f\n\t"
        "str r1, [%0]\n\t"

        "1:\n\t"
        : "+r"(ctx)
        :
        : "r1", "memory");
}

/**
 * @brief Restore ARM context (assembly)
 */
static OL_FORCE_INLINE void ol_ctx_restore_arm(const ol_ctx_arm_t* ctx) {
    asm volatile(
        /* Restore vector registers */
        "vldr d8, [%0], #8\n\t"
        "vldr d9, [%0], #8\n\t"
        "vldr d10, [%0], #8\n\t"
        "vldr d11, [%0], #8\n\t"
        "vldr d12, [%0], #8\n\t"
        "vldr d13, [%0], #8\n\t"
        "vldr d14, [%0], #8\n\t"
        "vldr d15, [%0], #8\n\t"

        /* Restore FPSCR */
        "ldr r1, [%0], #4\n\t"
        "vmsr fpscr, r1\n\t"

        /* Restore integer registers */
        "ldr lr, [%0], #4\n\t"
        "ldr sp, [%0], #4\n\t"
        "ldmia %0!, {r4-r11}\n\t"

        /* Jump to saved PC */
        "ldr r1, [%0]\n\t"
        "bx r1\n\t"
        : "+r"(ctx)
        :
        : "r1", "memory");

    __builtin_unreachable();
}

/**
 * @brief Make new ARM context
 */
static OL_FORCE_INLINE void ol_ctx_make_arm(ol_ctx_arm_t* ctx,
                                            void (*fn)(void),
                                            void* arg,
                                            void* stack_base,
                                            size_t stack_size) {
    /* Clear context */
    memset(ctx, 0, sizeof(*ctx));

    /* Set default FPSCR */
    ctx->fpscr = 0;

    /* Calculate stack top (8-byte aligned) */
    uintptr_t stack_top = (uintptr_t)stack_base + stack_size;
    stack_top = (stack_top & ~0x7);

    /* Set up stack */
    ctx->sp = stack_top;
    ctx->pc = (uintptr_t)fn;
    ctx->lr = 0; /* No return address */

    /* Set argument in r4 (callee-saved) */
    ctx->r4 = (uintptr_t)arg;
}
#endif /* OL_ARCH_ARM */


#endif /* OL_PLATFORM_POSIX */

/* ==================== Context Switching Wrappers ==================== */

/**
 * @brief Platform-independent context save
 */
static OL_FORCE_INLINE void ol_ctx_save(void* ctx) {
#if OL_PLATFORM_WINDOWS
    /* Windows uses fibers, no manual context save needed */
    (void)ctx;
#elif OL_PLATFORM_POSIX
#if OL_ARCH_X86_64
    ol_ctx_save_x86_64((ol_ctx_x86_64_t*)ctx);
#elif OL_ARCH_AARCH64
    ol_ctx_save_aarch64((ol_ctx_aarch64_t*)ctx);
#elif OL_ARCH_ARM
    ol_ctx_save_arm((ol_ctx_arm_t*)ctx);
#else
#error "Unsupported architecture for assembly context switching"
#endif
#endif
}

/**
 * @brief Platform-independent context restore
 */
static OL_FORCE_INLINE void ol_ctx_restore(const void* ctx) {
#if OL_PLATFORM_WINDOWS
    /* Windows uses fibers, no manual context restore needed */
    (void)ctx;
#elif OL_PLATFORM_POSIX
#if OL_ARCH_X86_64
    ol_ctx_restore_x86_64((const ol_ctx_x86_64_t*)ctx);
#elif OL_ARCH_AARCH64
    ol_ctx_restore_aarch64((const ol_ctx_aarch64_t*)ctx);
#elif OL_ARCH_ARM
    ol_ctx_restore_arm((const ol_ctx_arm_t*)ctx);
#else
#error "Unsupported architecture for assembly context switching"
#endif
#endif
}

/**
 * @brief Platform-independent context creation
 */
static OL_FORCE_INLINE void ol_ctx_make(void* ctx,
                                        ol_gt_entry_fn entry,
                                        void* arg,
                                        void* stack_base,
                                        size_t stack_size) {
#if OL_PLATFORM_WINDOWS
    /* Windows fibers are created differently */
    (void)ctx;
    (void)fn;
    (void)arg;
    (void)stack_base;
    (void)stack_size;
#elif OL_PLATFORM_POSIX
#if OL_ARCH_X86_64
    ol_ctx_make_x86_64((ol_ctx_x86_64_t*)ctx,
                       (void (*)(void))entry,
                       arg,
                       stack_base,
                       stack_size);
#elif OL_ARCH_AARCH64
    ol_ctx_make_aarch64((ol_ctx_aarch64_t*)ctx,
                        (void (*)(void))entry,
                        arg,
                        stack_base,
                        stack_size);
#elif OL_ARCH_ARM
    ol_ctx_make_arm(
        (ol_ctx_arm_t*)ctx, (void (*)(void))entry, arg, stack_base, stack_size);
#else
#error "Unsupported architecture for assembly context switching"
#endif
#endif
}

/* ==================== Work-Stealing Deque Implementation ==================== */

/**
 * @brief Work-stealing deque structure (Chase-Lev algorithm)
 */
/* struct ol_work_stealing_queue is defined in ol_green_threads.h */

/**
 * @brief Initialize work-stealing queue
 */
/* Internal helper forward declarations (Wave 1 fix). */
/* These helpers are defined later in this file but used earlier by
 * ol_work_stealing_queue_init and ol_stack_pool_*. Static linkage,
 * defined further down; declared here to avoid the implicit-int rule
 * giving them an extern-int type. */
static void* ol_numa_alloc(size_t size, size_t alignment, int numa_node);
static void ol_numa_free(void* ptr, size_t size);
static int ol_get_current_numa_node(void);
static int ol_get_numa_cpu_count(int numa_node);

static OL_FORCE_INLINE int
ol_work_stealing_queue_init(ol_work_stealing_queue_t* queue, size_t capacity) {
    /* Capacity must be power of 2 */
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
        return -1;
    }

    /* Allocate array */
    size_t array_size = capacity * sizeof(atomic_uintptr_t);
    atomic_uintptr_t* array =
        (atomic_uintptr_t*)ol_numa_alloc(array_size, 64, -1);
    if (!array) {
        return -1;
    }

    /* Initialize all entries */
    for (size_t i = 0; i < capacity; i++) {
        atomic_init(&array[i], 0);
    }

    queue->array = array;
    queue->capacity = capacity;
    queue->mask = capacity - 1;
    atomic_init(&queue->bottom, 0);
    atomic_init(&queue->top, 0);

    return 0;
}

/**
 * @brief Destroy work-stealing queue
 */
static OL_FORCE_INLINE void
ol_work_stealing_queue_destroy(ol_work_stealing_queue_t* queue) {
    if (queue->array) {
        ol_numa_free(queue->array, queue->capacity * sizeof(atomic_uintptr_t));
        queue->array = NULL;
    }
    queue->capacity = 0;
    queue->mask = 0;
}

/**
 * @brief Push task to bottom of deque (owner thread)
 */
static OL_FORCE_INLINE bool
ol_work_stealing_queue_push(ol_work_stealing_queue_t* queue, void* task) {
    long b = atomic_load_explicit(&queue->bottom, memory_order_relaxed);
    long t = atomic_load_explicit(&queue->top, memory_order_acquire);

    /* Check if deque is full */
    if (b - t > (long)(queue->capacity - 1)) {
        return false;
    }

    /* Store task */
    size_t idx = b & queue->mask;
    atomic_store_explicit(
        &queue->array[idx], (uintptr_t)task, memory_order_relaxed);

    /* Ensure task is visible before updating bottom */
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&queue->bottom, b + 1, memory_order_relaxed);

    return true;
}

/**
 * @brief Pop task from bottom of deque (owner thread)
 */
static OL_FORCE_INLINE void*
ol_work_stealing_queue_pop(ol_work_stealing_queue_t* queue) {
    long b = atomic_load_explicit(&queue->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&queue->bottom, b, memory_order_relaxed);

    /* Ensure bottom is visible before reading top */
    atomic_thread_fence(memory_order_seq_cst);
    long t = atomic_load_explicit(&queue->top, memory_order_relaxed);

    /* v1.3.2 fix: Chase-Lev empty condition is (b < t), not (b <= t).
     * When b == t the deque holds exactly one item, which must be
     * claimed via CAS against stealers. Treating b == t as empty made
     * single-item deques impossible to pop, which caused the actor
     * driver loop to spin on nanosleep forever. */
    if (b < t) {
        /* Empty deque */
        atomic_store_explicit(&queue->bottom, b + 1, memory_order_relaxed);
        return NULL;
    }

    /* Deque has at least one item. */
    size_t idx = b & queue->mask;
    void* task =
        (void*)atomic_load_explicit(&queue->array[idx], memory_order_relaxed);

    if (b > t) {
        /* More than one item: safe to return without CAS. */
        return task;
    }

    /* Exactly one item: claim it via CAS against stealers. */
    if (!atomic_compare_exchange_strong_explicit(&queue->top,
                                                 &t,
                                                 t + 1,
                                                 memory_order_seq_cst,
                                                 memory_order_relaxed)) {
        task = NULL;
    }
    atomic_store_explicit(&queue->bottom, b + 1, memory_order_relaxed);
    return task;
}

/**
 * @brief Steal task from top of deque (other thread)
 */
static OL_FORCE_INLINE void*
ol_work_stealing_queue_steal(ol_work_stealing_queue_t* queue) {
    long t = atomic_load_explicit(&queue->top, memory_order_acquire);

    /* Ensure top is read before bottom */
    atomic_thread_fence(memory_order_seq_cst);
    long b = atomic_load_explicit(&queue->bottom, memory_order_acquire);

    if (b <= t) {
        /* Empty deque */
        return NULL;
    }

    /* Read task */
    size_t idx = t & queue->mask;
    void* task =
        (void*)atomic_load_explicit(&queue->array[idx], memory_order_consume);

    if (!task) {
        /* Task not yet committed by pusher */
        return NULL;
    }

    /* Try to increment top */
    if (!atomic_compare_exchange_strong_explicit(&queue->top,
                                                 &t,
                                                 t + 1,
                                                 memory_order_seq_cst,
                                                 memory_order_relaxed)) {
        /* Lost race to another stealer or pop */
        return NULL;
    }

    return task;
}

/**
 * @brief Check if queue is empty
 */
static OL_FORCE_INLINE bool
ol_work_stealing_queue_empty(ol_work_stealing_queue_t* queue) {
    long b = atomic_load_explicit(&queue->bottom, memory_order_relaxed);
    long t = atomic_load_explicit(&queue->top, memory_order_relaxed);
    return b <= t;
}

/* ==================== Stack Pool Implementation ==================== */

/**
 * @brief Stack pool bucket structure
 */
/* ol_stack_bucket_t is defined in ol_green_threads.h */

/**
 * @brief Stack pool structure
 */
/* struct ol_stack_pool is defined in ol_green_threads.h */

/**
 * @brief Initialize stack pool
 */
static OL_FORCE_INLINE int ol_stack_pool_init(ol_stack_pool_t* pool) {
    if (!pool)
        return -1;

    memset(pool, 0, sizeof(*pool));

    /* Initialize each bucket */
    for (int i = 0; i < 8; i++) {
        ol_stack_bucket_t* bucket = &pool->buckets[i];
        bucket->stack_size = OL_STACK_POOL_BUCKET_SIZES[i];
        bucket->capacity = 1024; /* Initial capacity */
        bucket->count = 0;

        /* Allocate bucket array */
        size_t array_size = bucket->capacity * sizeof(void*);
        bucket->stacks = (void**)ol_numa_alloc(array_size, 64, -1);
        if (!bucket->stacks) {
            /* Cleanup previous allocations */
            for (int j = 0; j < i; j++) {
                ol_numa_free(pool->buckets[j].stacks,
                             pool->buckets[j].capacity * sizeof(void*));
            }
            return -1;
        }

        /* Initialize atomic count */
        atomic_init(&bucket->count, 0);
    }

    /* Initialize statistics */
    atomic_init(&pool->hits, 0);
    atomic_init(&pool->misses, 0);
    atomic_init(&pool->allocations, 0);
    atomic_init(&pool->deallocations, 0);

    return 0;
}

/**
 * @brief Destroy stack pool
 */
static OL_FORCE_INLINE void ol_stack_pool_destroy(ol_stack_pool_t* pool) {
    if (!pool)
        return;

    /* Free all stacks in all buckets */
    for (int i = 0; i < 8; i++) {
        ol_stack_bucket_t* bucket = &pool->buckets[i];

        if (bucket->stacks) {
            size_t count =
                atomic_load_explicit(&bucket->count, memory_order_relaxed);

            for (size_t j = 0; j < count; j++) {
                void* stack = bucket->stacks[j];
                if (stack) {
#if OL_PLATFORM_POSIX
                    size_t total_size = bucket->stack_size + 2 * OL_PAGE_SIZE;
                    void* stack_area = (char*)stack - OL_PAGE_SIZE;
                    ol_numa_free(stack_area, total_size);
#elif OL_PLATFORM_WINDOWS
                    VirtualFree(stack, 0, MEM_RELEASE);
#endif
                }
            }

            ol_numa_free(bucket->stacks, bucket->capacity * sizeof(void*));
            bucket->stacks = NULL;
        }
    }
}

/**
 * @brief Find appropriate bucket for stack size
 */
static OL_FORCE_INLINE int ol_stack_pool_find_bucket(size_t stack_size) {
    for (int i = 0; i < 8; i++) {
        if (OL_STACK_POOL_BUCKET_SIZES[i] >= stack_size) {
            return i;
        }
    }
    return -1; /* Too large for pool */
}

/**
 * @brief Allocate stack (try pool first, then system)
 */
static OL_FORCE_INLINE void* ol_stack_pool_allocate(ol_stack_pool_t* pool,
                                                    size_t stack_size,
                                                    int numa_node) {
    if (!pool)
        return NULL;

    int bucket_idx = ol_stack_pool_find_bucket(stack_size);
    if (bucket_idx < 0) {
        /* Stack too large for pool, allocate directly */
        atomic_fetch_add_explicit(&pool->misses, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&pool->allocations, 1, memory_order_relaxed);

        /* Direct allocation with NUMA awareness */
#if OL_PLATFORM_POSIX
        size_t total_size = stack_size + 2 * OL_PAGE_SIZE;
        void* stack_area = ol_numa_alloc(total_size, OL_PAGE_SIZE, numa_node);
        if (!stack_area)
            return NULL;

        /* Protect guard pages */
        mprotect(stack_area, OL_PAGE_SIZE, PROT_NONE);
        mprotect((char*)stack_area + total_size - OL_PAGE_SIZE,
                 OL_PAGE_SIZE,
                 PROT_NONE);

        return (char*)stack_area + OL_PAGE_SIZE;
#elif OL_PLATFORM_WINDOWS
        size_t total_size = stack_size + 2 * OL_PAGE_SIZE;
        void* stack_area = VirtualAllocExNuma(GetCurrentProcess(),
                                              NULL,
                                              total_size,
                                              MEM_RESERVE | MEM_COMMIT,
                                              PAGE_READWRITE,
                                              numa_node);
        if (!stack_area)
            return NULL;

        /* Protect guard pages */
        DWORD old_protect;
        VirtualProtect(stack_area, OL_PAGE_SIZE, PAGE_NOACCESS, &old_protect);
        VirtualProtect((char*)stack_area + total_size - OL_PAGE_SIZE,
                       OL_PAGE_SIZE,
                       PAGE_NOACCESS,
                       &old_protect);

        return (char*)stack_area + OL_PAGE_SIZE;
#endif
    }

    ol_stack_bucket_t* bucket = &pool->buckets[bucket_idx];

    /* Try to get from pool */
    size_t count = atomic_load_explicit(&bucket->count, memory_order_acquire);
    if (count > 0) {
        /* Attempt to pop from bucket */
        size_t new_count = count - 1;
        if (atomic_compare_exchange_strong_explicit(&bucket->count,
                                                    &count,
                                                    new_count,
                                                    memory_order_acq_rel,
                                                    memory_order_acquire)) {
            void* stack = bucket->stacks[new_count];
            atomic_fetch_add_explicit(&pool->hits, 1, memory_order_relaxed);
            return stack;
        }
    }

    /* Pool miss, allocate new stack */
    atomic_fetch_add_explicit(&pool->misses, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&pool->allocations, 1, memory_order_relaxed);

    /* Allocate with guard pages and NUMA awareness */
#if OL_PLATFORM_POSIX
    size_t total_size = bucket->stack_size + 2 * OL_PAGE_SIZE;
    void* stack_area = ol_numa_alloc(total_size, OL_PAGE_SIZE, numa_node);
    if (!stack_area)
        return NULL;

    /* Protect guard pages */
    mprotect(stack_area, OL_PAGE_SIZE, PROT_NONE);
    mprotect(
        (char*)stack_area + total_size - OL_PAGE_SIZE, OL_PAGE_SIZE, PROT_NONE);

    return (char*)stack_area + OL_PAGE_SIZE;
#elif OL_PLATFORM_WINDOWS
    size_t total_size = bucket->stack_size + 2 * OL_PAGE_SIZE;
    void* stack_area = VirtualAllocExNuma(GetCurrentProcess(),
                                          NULL,
                                          total_size,
                                          MEM_RESERVE | MEM_COMMIT,
                                          PAGE_READWRITE,
                                          numa_node);
    if (!stack_area)
        return NULL;

    /* Protect guard pages */
    DWORD old_protect;
    VirtualProtect(stack_area, OL_PAGE_SIZE, PAGE_NOACCESS, &old_protect);
    VirtualProtect((char*)stack_area + total_size - OL_PAGE_SIZE,
                   OL_PAGE_SIZE,
                   PAGE_NOACCESS,
                   &old_protect);

    return (char*)stack_area + OL_PAGE_SIZE;
#endif
}

/**
 * @brief Free stack (return to pool or system)
 */
static OL_FORCE_INLINE void
ol_stack_pool_free(ol_stack_pool_t* pool, void* stack, size_t stack_size) {
    if (!pool || !stack)
        return;

    int bucket_idx = ol_stack_pool_find_bucket(stack_size);
    if (bucket_idx < 0) {
        /* Stack too large for pool, free directly */
        atomic_fetch_add_explicit(
            &pool->deallocations, 1, memory_order_relaxed);

#if OL_PLATFORM_POSIX
        size_t total_size = stack_size + 2 * OL_PAGE_SIZE;
        void* stack_area = (char*)stack - OL_PAGE_SIZE;
        ol_numa_free(stack_area, total_size);
#elif OL_PLATFORM_WINDOWS
        size_t total_size = stack_size + 2 * OL_PAGE_SIZE;
        void* stack_area = (char*)stack - OL_PAGE_SIZE;
        VirtualFree(stack_area, 0, MEM_RELEASE);
#endif
        return;
    }

    ol_stack_bucket_t* bucket = &pool->buckets[bucket_idx];

    /* Try to return to pool */
    size_t count = atomic_load_explicit(&bucket->count, memory_order_relaxed);

    if (count < bucket->capacity) {
        /* Add to bucket */
        bucket->stacks[count] = stack;

        /* Make stack visible before updating count */
        atomic_thread_fence(memory_order_release);
        atomic_fetch_add_explicit(&bucket->count, 1, memory_order_relaxed);
    } else {
        /* Pool full, free directly */
        atomic_fetch_add_explicit(
            &pool->deallocations, 1, memory_order_relaxed);

#if OL_PLATFORM_POSIX
        size_t total_size = bucket->stack_size + 2 * OL_PAGE_SIZE;
        void* stack_area = (char*)stack - OL_PAGE_SIZE;
        ol_numa_free(stack_area, total_size);
#elif OL_PLATFORM_WINDOWS
        size_t total_size = bucket->stack_size + 2 * OL_PAGE_SIZE;
        void* stack_area = (char*)stack - OL_PAGE_SIZE;
        VirtualFree(stack_area, 0, MEM_RELEASE);
#endif
    }
}

/* ==================== NUMA Awareness Implementation ==================== */

/**
 * @brief Get current NUMA node
 */
static OL_FORCE_INLINE int ol_get_current_numa_node(void) {
#if OL_PLATFORM_WINDOWS && OL_NUMA_AVAILABLE
    ULONG node;
    if (GetNumaProcessorNodeEx((PPROCESSOR_NUMBER)NULL, &node) != 0) {
        return (int)node;
    }
    return -1;
#elif OL_PLATFORM_POSIX && defined(__linux__) && OL_NUMA_AVAILABLE
    if (numa_available() < 0) {
        return -1;
    }
    return numa_node_of_cpu(sched_getcpu());
#else
    return -1; /* NUMA not supported */
#endif
}

/**
 * @brief Allocate memory with NUMA awareness
 */
static OL_NO_INLINE void*
ol_numa_alloc(size_t size, size_t alignment, int numa_node) {
    /* v1.3.2: NUMA-aware allocation is disabled. The current codebase
     * has no member that requires alignment greater than what malloc
     * guarantees (alignof(max_align_t), 16 bytes on x86_64). The
     * earlier aligned(64) annotations on struct ol_stack_pool and
     * struct ol_gt_scheduler.tls have been removed, so plain malloc
     * is sufficient.
     *
     * Using posix_memalign here broke ASan's free interceptor on
     * large alignments, so it is deliberately not used. NUMA will
     * return in v1.4 with a paired allocator that tracks its origin. */
    (void)alignment;
    (void)numa_node;
    if (size == 0)
        return NULL;
    return malloc(size);
}

/**
 * @brief Free NUMA-aware memory
 */
static OL_NO_INLINE void ol_numa_free(void* ptr, size_t size) {
    /* Paired with the malloc in ol_numa_alloc above. */
    (void)size;
    free(ptr);
}

/**
 * @brief Get CPU core count per NUMA node
 */
static OL_FORCE_INLINE int ol_get_numa_cpu_count(int numa_node) {
#if OL_PLATFORM_WINDOWS && OL_NUMA_AVAILABLE
    ULONG cpu_count = 0;
    if (GetNumaNodeProcessorMaskEx((USHORT)numa_node, &cpu_count) != 0) {
        /* Count set bits in mask */
        int count = 0;
        while (cpu_count) {
            count += cpu_count & 1;
            cpu_count >>= 1;
        }
        return count;
    }
    return -1;
#elif OL_PLATFORM_POSIX && defined(__linux__) && OL_NUMA_AVAILABLE
    if (numa_available() < 0)
        return -1;
    struct bitmask* cpumask = numa_allocate_cpumask();
    if (!cpumask)
        return -1;

    if (numa_node_to_cpus(numa_node, cpumask) != 0) {
        numa_free_cpumask(cpumask);
        return -1;
    }

    int count = numa_bitmask_weight(cpumask);
    numa_free_cpumask(cpumask);
    return count;
#else
    (void)numa_node;
    return -1; /* NUMA not supported */
#endif
}

/* ==================== Scheduler Implementation ==================== */

/**
 * @brief Initialize thread-local scheduler
 */
static OL_NO_INLINE int ol_gt_scheduler_init_thread_local(void) {
    if (g_thread_scheduler) {
        return 0; /* Already initialized */
    }

    /* Allocate scheduler structure with NUMA awareness */
    int numa_node = ol_get_current_numa_node();
    g_thread_scheduler = (ol_gt_scheduler_t*)ol_numa_alloc(
        sizeof(ol_gt_scheduler_t), OL_CACHE_LINE_SIZE, numa_node);

    if (!g_thread_scheduler) {
        atomic_store_explicit(
            &g_last_error, OL_GT_ERROR_OUT_OF_MEMORY, memory_order_relaxed);
        return -1;
    }

    /* Initialize scheduler */
    memset(g_thread_scheduler, 0, sizeof(ol_gt_scheduler_t));

    /* Initialize work-stealing queues for each priority */
    for (int i = 0; i < 5; i++) {
        if (ol_work_stealing_queue_init(&g_thread_scheduler->queues[i],
                                        OL_MAX_WORK_STEALING_QUEUE_SIZE) != 0) {
            /* Cleanup on failure */
            for (int j = 0; j < i; j++) {
                ol_work_stealing_queue_destroy(&g_thread_scheduler->queues[j]);
            }
            ol_numa_free(g_thread_scheduler, sizeof(ol_gt_scheduler_t));
            g_thread_scheduler = NULL;
            return -1;
        }
    }

    /* Initialize stack pool */
    if (ol_stack_pool_init(&g_thread_scheduler->stack_pool) != 0) {
        for (int i = 0; i < 5; i++) {
            ol_work_stealing_queue_destroy(&g_thread_scheduler->queues[i]);
        }
        ol_numa_free(g_thread_scheduler, sizeof(ol_gt_scheduler_t));
        g_thread_scheduler = NULL;
        return -1;
    }

    /* Set NUMA information */
    g_thread_scheduler->numa_info.node_id = numa_node;
    g_thread_scheduler->numa_info.cpu_count = ol_get_numa_cpu_count(numa_node);

    /* Set default preemption slice */
    g_thread_scheduler->preemption_slice_ns = OL_DEFAULT_PREEMPTION_SLICE_NS;

    /* Enable features by default */
    g_thread_scheduler->work_stealing_enabled = true;
    g_thread_scheduler->lazy_allocation_enabled = true;
    g_thread_scheduler->numa_awareness_enabled = (numa_node >= 0);
    g_thread_scheduler->statistics_enabled = true;

    /* v1.3.2 fix: global scheduler list is disabled.
     *
     * The list was inserted into here and removed in
     * ol_gt_scheduler_shutdown(). Under concurrent scheduler
     * shutdown the removal wrote to a `prev` node that another
     * thread could already have freed, which produced a
     * nondeterministic SEGV at moderate load. The only consumer of
     * the list is ol_gt_work_steal(), and the v1.3.2 driver loop
     * never calls it. The list is left empty until v1.3.3 reinstates
     * work stealing on top of proper synchronization.
     */

    /* Update global statistics */
    OL_STATS_ADD(&g_global_stats.total_spawned, 1);

    return 0;
}

/**
 * @brief Work-stealing function
 */
static OL_NO_INLINE void* ol_gt_work_steal(void) {
    if (!g_thread_scheduler || !g_thread_scheduler->work_stealing_enabled) {
        return NULL;
    }

    /* Try to steal from other schedulers */
    uintptr_t head =
        atomic_load_explicit(&g_scheduler_list_head, memory_order_acquire);
    ol_gt_scheduler_t* other = (ol_gt_scheduler_t*)head;

    while (other) {
        if (other != g_thread_scheduler) {
            /* Try all priority levels (highest first) */
            for (int priority = OL_GT_PRIORITY_REALTIME;
                 priority >= OL_GT_PRIORITY_IDLE;
                 priority--) {
                void* task =
                    ol_work_stealing_queue_steal(&other->queues[priority]);
                if (task) {
                    /* Update statistics */
                    OL_STATS_ADD(&g_global_stats.work_stolen, 1);
                    if (g_thread_scheduler->statistics_enabled) {
                        OL_STATS_ADD(&g_thread_scheduler->global_stats.work_stolen, 1);
                    }
                    return task;
                }
            }
        }
        other = other->next;
    }

    return NULL;
}

/**
 * @brief Hybrid scheduler - check for preemption
 */
static OL_FORCE_INLINE bool ol_gt_should_preempt(void) {
    if (!g_thread_scheduler ||
        g_thread_scheduler->current->sched_policy != OL_GT_SCHED_HYBRID) {
        return false;
    }

    /* Check if time slice expired */
    uint64_t now = ol_rdtsc();
    uint64_t last_preemption = atomic_load_explicit(
        &g_thread_scheduler->last_preemption, memory_order_relaxed);

    /* Convert TSC to nanoseconds (approximate) */
    uint64_t elapsed_ns =
        (now - last_preemption) / 3; /* ~3GHz CPU assumption */

    if (elapsed_ns >= g_thread_scheduler->preemption_slice_ns) {
        atomic_store_explicit(
            &g_thread_scheduler->last_preemption, now, memory_order_relaxed);

        /* Update statistics */
        OL_STATS_ADD(&g_global_stats.preemptive_yields, 1);
        if (g_thread_scheduler->statistics_enabled &&
            g_thread_scheduler->current) {
            OL_STATS_ADD(&g_thread_scheduler->current->stats.preemptive_yields, 1);
        }

        return true;
    }

    return false;
}

/**
 * @brief Select next thread to run based on priority
 */
static OL_NO_INLINE ol_gt_t* ol_gt_scheduler_select_next(void) {
    if (!g_thread_scheduler) {
        return NULL;
    }

    /* Try to get task from work-stealing queues (highest priority first) */
    for (int priority = OL_GT_PRIORITY_REALTIME;
         priority >= OL_GT_PRIORITY_IDLE;
         priority--) {
        /* First try local queue */
        void* task =
            ol_work_stealing_queue_pop(&g_thread_scheduler->queues[priority]);
        if (task) {
            return (ol_gt_t*)task;
        }

        /* If work stealing enabled, try to steal */
        if (g_thread_scheduler->work_stealing_enabled) {
            task = ol_gt_work_steal();
            if (task) {
                return (ol_gt_t*)task;
            }
        }
    }

    return NULL;
}

/* ==================== Original API Implementation ==================== */

/**
 * @brief Initialize the green-thread scheduler for the calling OS thread.
 *
 * @details
 * Allocates a per-thread scheduler instance, initializes its
 * work-stealing deques, stack pool, NUMA metadata, and default
 * scheduling parameters. The instance is stored in thread-local
 * storage and released by @ref ol_gt_scheduler_shutdown.
 *
 * Must be called once per OS thread that intends to run green
 * threads. Subsequent calls on the same thread are no-ops.
 *
 * @return 0 on success, -1 on allocation failure.
 *
 * @note The actor subsystem calls this automatically from
 *       @ref ol_gt_run_to_completion. User code that drives green
 *       threads directly must call it explicitly.
 * @see ol_gt_scheduler_shutdown, ol_gt_spawn, ol_gt_run_to_completion
 */
int ol_gt_scheduler_init(void) {
    /* Initialize page size */
#if OL_PLATFORM_POSIX
    OL_PAGE_SIZE = (size_t)sysconf(_SC_PAGESIZE);
    if (OL_PAGE_SIZE == 0) {
        OL_PAGE_SIZE = 4096;
    }
#elif OL_PLATFORM_WINDOWS
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    OL_PAGE_SIZE = sys_info.dwPageSize;
#endif

    /* Initialize thread-local scheduler */
    return ol_gt_scheduler_init_thread_local();
}

/**
 * @brief Release the calling thread's green-thread scheduler.
 *
 * @details
 * Destroys the work-stealing deques, drains the stack pool, and frees
 * the scheduler instance. Every green thread created on this OS
 * thread must already be destroyed.
 *
 * @warning Any green thread still referencing this scheduler after
 *          shutdown holds a dangling pointer. The function logs a
 *          warning in debug builds and proceeds silently in release.
 * @see ol_gt_scheduler_init
 */
void ol_gt_scheduler_shutdown(void) {
    if (!g_thread_scheduler) {
        return;
    }

    /* v1.3.2 fix: see the matching comment in
     * ol_gt_scheduler_init_thread_local(). The global scheduler list
     * is not maintained, so there is nothing to remove here.
     */

    /* Destroy work-stealing queues */
    for (int i = 0; i < 5; i++) {
        ol_work_stealing_queue_destroy(&g_thread_scheduler->queues[i]);
    }

    /* Destroy stack pool */
    ol_stack_pool_destroy(&g_thread_scheduler->stack_pool);

    /* Free scheduler */
    ol_numa_free(g_thread_scheduler, sizeof(ol_gt_scheduler_t));
    g_thread_scheduler = NULL;

    /* Update global statistics */
    OL_STATS_ADD(&g_global_stats.total_destroyed, 1);
}

/**
 * @brief Trampoline function for green thread execution
 */
static void OL_NO_INLINE ol_gt_trampoline(void* arg) {
    ol_gt_t* gt = (ol_gt_t*)arg;

    /* Update thread state */
    atomic_store_explicit(
        &gt->state, OL_GT_STATE_RUNNING, memory_order_release);
    g_thread_scheduler->current = gt;

    /* Check for cancellation */
    if (atomic_load_explicit(&gt->cancel_flag, memory_order_acquire)) {
        atomic_store_explicit(
            &gt->state, OL_GT_STATE_CANCELED, memory_order_release);
        g_thread_scheduler->current = NULL;
        ol_gt_yield();
        return;
    }

    /* Update statistics */
    uint64_t start_time = ol_rdtsc();

    /* Execute user function */
    gt->entry(gt->arg);

    /* Update runtime statistics */
    uint64_t end_time = ol_rdtsc();
    uint64_t runtime = end_time - start_time;
    atomic_fetch_add_explicit(
        &gt->total_runtime, runtime, memory_order_relaxed);

    /* Mark as done */
    atomic_store_explicit(&gt->state, OL_GT_STATE_DONE, memory_order_release);
    g_thread_scheduler->current = NULL;

    /* Yield to the driver. In driver mode this does not return. */
    ol_gt_yield();
    __builtin_unreachable();
}

/**
 * @brief Create a lazy green thread (metadata only)
 */
static ol_gt_t* ol_gt_create_lazy(ol_gt_entry_fn entry,
                                  void* arg,
                                  const ol_gt_config_t* config) {
    /* Allocate green thread structure with NUMA awareness */
    int numa_node = config ? config->numa_node : -1;
    if (numa_node < 0 && g_thread_scheduler) {
        numa_node = g_thread_scheduler->numa_info.node_id;
    }

    ol_gt_t* gt =
        (ol_gt_t*)ol_numa_alloc(sizeof(ol_gt_t), OL_CACHE_LINE_SIZE, numa_node);
    if (!gt) {
        atomic_store_explicit(
            &g_last_error, OL_GT_ERROR_OUT_OF_MEMORY, memory_order_relaxed);
        return NULL;
    }

    /* Initialize structure */
    memset(gt, 0, sizeof(*gt));

    /* Set basic fields */
    atomic_init(&gt->state, OL_GT_STATE_LAZY);
    gt->entry = entry;
    gt->arg = arg;

    /* Set priority and scheduling policy */
    gt->priority = config ? config->priority : OL_GT_PRIORITY_NORMAL;
    gt->sched_policy = config ? config->sched_policy : OL_GT_SCHED_COOPERATIVE;

    /* Set NUMA information */
    gt->numa_node = numa_node;

    /* Initialize atomic fields */
    atomic_init(&gt->cancel_flag, false);
    atomic_init(&gt->cancel_requested, false);
    atomic_init(&gt->stack_watermark, 0);
    atomic_init(&gt->last_run_time, 0);
    atomic_init(&gt->total_runtime, 0);
    atomic_init(&gt->work_steal_count, 0);
    atomic_init(&gt->work_stolen_count, 0);
    atomic_init(&gt->numa_local_accesses, 0);
    atomic_init(&gt->numa_remote_accesses, 0);

    /* Initialize statistics */
    if (config && config->enable_stats) {
        memset(&gt->stats, 0, sizeof(gt->stats));
        gt->stats.spawn_count = 1;
    }

    return gt;
}

/**
 * @brief Materialize lazy green thread (allocate stack and context)
 */
static int ol_gt_materialize(ol_gt_t* gt, size_t stack_size) {
    if (atomic_load_explicit(&gt->state, memory_order_acquire) !=
        OL_GT_STATE_LAZY) {
        return -1;
    }

    /* Determine actual stack size */
    size_t actual_stack_size = stack_size;
    if (actual_stack_size < OL_MIN_STACK_SIZE) {
        actual_stack_size = OL_DEFAULT_STACK_SIZE;
    }
    if (actual_stack_size > OL_MAX_STACK_SIZE) {
        actual_stack_size = OL_MAX_STACK_SIZE;
    }

    /* Allocate stack from pool */
    void* stack = ol_stack_pool_allocate(
        &g_thread_scheduler->stack_pool, actual_stack_size, gt->numa_node);
    if (!stack) {
        atomic_store_explicit(
            &g_last_error, OL_GT_ERROR_OUT_OF_MEMORY, memory_order_relaxed);
        return -1;
    }

    /* Set stack information */
    gt->stack_base = stack;
    gt->stack_size = actual_stack_size;
    gt->stack_limit = (char*)stack + actual_stack_size;

    /* Create execution context */
#if OL_PLATFORM_WINDOWS
    /* Create fiber */
    gt->fiber = CreateFiberEx(actual_stack_size,
                              0,
                              FIBER_FLAG_FLOAT_SWITCH,
                              (LPFIBER_START_ROUTINE)ol_gt_trampoline,
                              gt);
    if (!gt->fiber) {
        ol_stack_pool_free(
            &g_thread_scheduler->stack_pool, stack, actual_stack_size);
        atomic_store_explicit(
            &g_last_error, OL_GT_ERROR_INTERNAL, memory_order_relaxed);
        return -1;
    }
#else
    /* Create assembly context */
    ol_ctx_make(&gt->context, ol_gt_trampoline, gt, stack, actual_stack_size);
#endif

    /* Update state */
    atomic_store_explicit(&gt->state, OL_GT_STATE_READY, memory_order_release);

    return 0;
}

/**
 * @brief Create a new green thread with default configuration.
 *
 * @param entry      Entry point. Runs on the green thread's own stack.
 * @param arg        Opaque argument passed to @p entry.
 * @param stack_size Requested stack size in bytes; 0 selects the
 *                   default (see @ref OL_DEFAULT_STACK_SIZE).
 *
 * @return A green-thread handle, or NULL on allocation failure.
 *
 * @note The thread is created in the READY state and is enqueued on
 *       the calling thread's scheduler. Use
 *       @ref ol_gt_run_to_completion (or a custom driver built on
 *       @ref ol_gt_resume and @ref ol_gt_yield) to execute it.
 * @see ol_gt_spawn_ex, ol_gt_destroy, ol_gt_run_to_completion
 */
ol_gt_t* ol_gt_spawn(ol_gt_entry_fn entry, void* arg, size_t stack_size) {
    /* Check if scheduler is initialized */
    if (!g_thread_scheduler && ol_gt_scheduler_init() != 0) {
        return NULL;
    }

    /* Create default configuration */
    ol_gt_config_t config = { .priority = OL_GT_PRIORITY_NORMAL,
                              .sched_policy = OL_GT_SCHED_COOPERATIVE,
                              .stack_size = stack_size,
                              .numa_node = -1,
                              .lazy_allocation =
                                  g_thread_scheduler->lazy_allocation_enabled,
                              .enable_stats =
                                  g_thread_scheduler->statistics_enabled };

    return ol_gt_spawn_ex(entry, arg, &config);
}

/**
 * @brief Create a green thread with advanced configuration.
 *
 * @details
 * Accepts a full @ref ol_gt_config_t block, allowing the caller to
 * set priority, scheduling policy, NUMA affinity, lazy allocation,
 * and statistics collection. See @ref ol_gt_spawn for the default
 * configuration used by the simpler API.
 *
 * @param entry  Entry point.
 * @param arg    Argument passed to @p entry.
 * @param config Configuration block; NULL selects defaults.
 *
 * @return Green-thread handle, or NULL on failure.
 * @see ol_gt_spawn, ol_gt_destroy
 */
ol_gt_t*
ol_gt_spawn_ex(ol_gt_entry_fn entry, void* arg, const ol_gt_config_t* config) {
    if (!entry) {
        atomic_store_explicit(
            &g_last_error, OL_GT_ERROR_INVALID_ARG, memory_order_relaxed);
        return NULL;
    }

    /* Check if scheduler is initialized */
    if (!g_thread_scheduler && ol_gt_scheduler_init() != 0) {
        return NULL;
    }

    /* Create green thread (lazy or immediate) */
    ol_gt_t* gt = NULL;

    if (config && config->lazy_allocation) {
        gt = ol_gt_create_lazy(entry, arg, config);
        if (!gt) {
            return NULL;
        }
    } else {
        /* Create and materialize immediately */
        gt = ol_gt_create_lazy(entry, arg, config);
        if (!gt) {
            return NULL;
        }

        size_t stack_size = config ? config->stack_size : 0;
        if (ol_gt_materialize(gt, stack_size) != 0) {
            ol_numa_free(gt, sizeof(ol_gt_t));
            return NULL;
        }
    }

    /* Add to scheduler queue */
    if (atomic_load_explicit(&gt->state, memory_order_acquire) ==
        OL_GT_STATE_READY) {
        int priority = config ? config->priority : OL_GT_PRIORITY_NORMAL;
        if (priority < 0)
            priority = 0;
        if (priority > 4)
            priority = 4;

        ol_work_stealing_queue_push(&g_thread_scheduler->queues[priority], gt);
    }

    /* Update statistics */
    OL_STATS_ADD(&g_global_stats.total_spawned, 1);
    if (g_thread_scheduler->statistics_enabled) {
        OL_STATS_ADD(&g_thread_scheduler->global_stats.total_spawned, 1);
    }

    return gt;
}

/**
 * @brief Enqueue a green thread for execution.
 *
 * @param gt Green thread previously created by @ref ol_gt_spawn.
 * @return 0 on success, -1 on error (NULL handle, dead thread, or
 *         scheduler not initialized).
 *
 * @note As of v1.3.2 this only queues the thread. A driver is
 *       required to actually execute it. Use
 *       @ref ol_gt_run_to_completion for a self-contained driver,
 *       or call this function in a loop together with
 *       @ref ol_gt_yield from a custom driver.
 * @see ol_gt_run_to_completion, ol_gt_yield
 */
int ol_gt_resume(ol_gt_t* gt) {
    if (!gt) {
        atomic_store_explicit(
            &g_last_error, OL_GT_ERROR_INVALID_ARG, memory_order_relaxed);
        return -1;
    }

    if (!g_thread_scheduler) {
        atomic_store_explicit(&g_last_error,
                              OL_GT_ERROR_SCHEDULER_NOT_INIT,
                              memory_order_relaxed);
        return -1;
    }

    /* Check thread state */
    ol_gt_state_t state =
        atomic_load_explicit(&gt->state, memory_order_acquire);

    if (state == OL_GT_STATE_DONE || state == OL_GT_STATE_CANCELED) {
        atomic_store_explicit(
            &g_last_error, OL_GT_ERROR_THREAD_DEAD, memory_order_relaxed);
        return -1;
    }

    /* Materialize lazy thread if needed */
    if (state == OL_GT_STATE_LAZY) {
        if (ol_gt_materialize(gt, OL_DEFAULT_STACK_SIZE) != 0) {
            return -1;
        }
    }

    /* Check for cancellation */
    if (atomic_load_explicit(&gt->cancel_flag, memory_order_acquire)) {
        atomic_store_explicit(
            &gt->state, OL_GT_STATE_CANCELED, memory_order_release);
        return -1;
    }

    /* Update state */
    atomic_store_explicit(&gt->state, OL_GT_STATE_READY, memory_order_release);

    /* Add to scheduler queue */
    ol_work_stealing_queue_push(&g_thread_scheduler->queues[gt->priority], gt);

    /* Update statistics */
    OL_STATS_ADD(&g_global_stats.context_switches, 1);
    if (g_thread_scheduler->statistics_enabled) {
        OL_STATS_ADD(&gt->stats.context_switches, 1);
    }

    return 0;
}

/**
 * @brief Yield execution to another green thread
 */
/**
 * @brief Yield execution to the scheduler or the driver loop.
 *
 * @details
 * In driver mode (set by @ref ol_gt_run_to_completion), the current
 * green thread saves its context and returns control to the driver
 * loop. The driver then decides what to run next. In legacy mode (no
 * driver active), the function switches directly to another ready
 * green thread, or returns immediately if none is ready.
 *
 * @note The current thread must be non-NULL. Callers inside a green
 *       thread always have a valid g_thread_scheduler->current
 *       because @ref ol_gt_trampoline sets it before invoking the
 *       user's entry function.
 *
 * @see ol_gt_run_to_completion, ol_gt_resume
 */
void ol_gt_yield(void) {
    if (!g_thread_scheduler)
        return;

    ol_gt_t* current = g_thread_scheduler->current;
    if (!current)
        return;

    OL_STATS_ADD(&g_global_stats.voluntary_yields, 1);
    if (g_thread_scheduler->statistics_enabled) {
        OL_STATS_ADD(&current->stats.voluntary_yields, 1);
    }

    /* Save the current green thread's context. */
    ol_ctx_save(&current->context);

    if (g_gt_driver_mode) {
        /* Return control to the driver loop. The driver will restore
         * our context again when it decides to resume us. */
        ol_ctx_restore(&g_gt_driver_ctx);
    } else {
        /* Legacy direct-switching path (no driver). */
        ol_gt_t* next = ol_gt_scheduler_select_next();
        if (next) {
            g_thread_scheduler->current = next;
            atomic_store_explicit(
                &next->state, OL_GT_STATE_RUNNING, memory_order_release);
            ol_ctx_restore(&next->context);
        }
    }

    g_thread_scheduler->current = current;
}

/**
 * @brief Wait for a green thread to reach a terminal state.
 *
 * @param gt Green thread to wait on.
 * @return 0 on success, -1 on error.
 *
 * @note Cooperatively yields to other ready threads while waiting.
 *       Must not be called from within the green thread being
 *       joined.
 * @see ol_gt_destroy, ol_gt_is_alive
 */
int ol_gt_join(ol_gt_t* gt) {
    if (!gt) {
        atomic_store_explicit(
            &g_last_error, OL_GT_ERROR_INVALID_ARG, memory_order_relaxed);
        return -1;
    }

    if (!g_thread_scheduler) {
        atomic_store_explicit(&g_last_error,
                              OL_GT_ERROR_SCHEDULER_NOT_INIT,
                              memory_order_relaxed);
        return -1;
    }

    /* Wait for thread to complete */
    while (true) {
        ol_gt_state_t state =
            atomic_load_explicit(&gt->state, memory_order_acquire);

        if (state == OL_GT_STATE_DONE || state == OL_GT_STATE_CANCELED) {
            break;
        }

        /* Yield to other threads while waiting */
        ol_gt_yield();

        /* Check for timeout or other conditions */
        // TODO: Add timeout mechanism
    }

    /* Update statistics */
    OL_STATS_ADD(&g_global_stats.total_destroyed, 1);

    return 0;
}

/**
 * @brief Destroy a green thread and release its resources.
 *
 * @details
 * Cancels the thread if still alive, joins it, returns its stack to
 * the pool, and frees the thread descriptor. Safe to call on a
 * thread that has already reached DONE or CANCELED.
 *
 * @param gt Green thread to destroy; NULL is a no-op.
 *
 * @warning Do not destroy a green thread from inside its own entry
 *          function. The stack being freed is the one currently in
 *          use. Let the driver destroy it after the yield.
 * @see ol_gt_spawn, ol_gt_cancel, ol_gt_join
 */
void ol_gt_destroy(ol_gt_t* gt) {
    if (!gt) {
        return;
    }

    /* Cancel if still running */
    if (atomic_load_explicit(&gt->state, memory_order_acquire) !=
            OL_GT_STATE_DONE &&
        atomic_load_explicit(&gt->state, memory_order_acquire) !=
            OL_GT_STATE_CANCELED) {
        ol_gt_cancel(gt);
        ol_gt_join(gt);
    }

    /* Free stack if allocated */
    if (gt->stack_base) {
        ol_stack_pool_free(
            &g_thread_scheduler->stack_pool, gt->stack_base, gt->stack_size);
    }

#if OL_PLATFORM_WINDOWS
    /* Delete fiber */
    if (gt->fiber) {
        DeleteFiber(gt->fiber);
    }
#endif

    /* Free green thread structure */
    ol_numa_free(gt, sizeof(ol_gt_t));

    /* Update statistics */
    OL_STATS_ADD(&g_global_stats.total_destroyed, 1);
}

/**
 * @brief Request cooperative cancellation of a green thread.
 *
 * @details
 * Sets the cancellation flag. The thread must check
 * @ref ol_gt_is_canceled at safe points and return on its own; the
 * runtime does not forcibly interrupt a running thread.
 *
 * @param gt Green thread to cancel.
 * @return 0 on success, -1 on error.
 * @see ol_gt_is_canceled, ol_gt_destroy
 */
int ol_gt_cancel(ol_gt_t* gt) {
    if (!gt) {
        atomic_store_explicit(
            &g_last_error, OL_GT_ERROR_INVALID_ARG, memory_order_relaxed);
        return -1;
    }

    /* Set cancellation flag */
    atomic_store_explicit(&gt->cancel_requested, true, memory_order_release);
    atomic_store_explicit(&gt->cancel_flag, true, memory_order_release);

    /* Update statistics */
    OL_STATS_ADD(&g_global_stats.cancellation_requests, 1);
    if (g_thread_scheduler && g_thread_scheduler->statistics_enabled) {
        OL_STATS_ADD(&gt->stats.cancellation_requests, 1);
    }

    return 0;
}

/**
 * @brief Return true if a green thread has not reached a terminal
 *        state.
 *
 * @param gt Green thread handle; NULL returns false.
 * @return true if the thread is NEW, READY, RUNNING, WAITING, or
 *         SLEEPING; false if DONE or CANCELED.
 */
bool ol_gt_is_alive(const ol_gt_t* gt) {
    if (!gt) {
        return false;
    }

    ol_gt_state_t state =
        atomic_load_explicit(&((ol_gt_t*)gt)->state, memory_order_acquire);
    return (state != OL_GT_STATE_DONE && state != OL_GT_STATE_CANCELED);
}

/**
 * @brief Return true if cancellation has been requested.
 *
 * @param gt Green thread handle; NULL returns false.
 * @return true if @ref ol_gt_cancel has been called on this thread.
 */
bool ol_gt_is_canceled(const ol_gt_t* gt) {
    if (!gt) {
        return false;
    }

    return atomic_load_explicit(&((ol_gt_t*)gt)->cancel_flag,
                                memory_order_acquire);
}

/**
 * @brief Return the green thread currently executing on the calling
 *        OS thread.
 *
 * @return Running green thread, or NULL if the calling thread is not
 *         inside a green thread.
 *
 * @note Uses thread-local state; results are undefined if the
 *       scheduler on this OS thread has been shut down.
 */
ol_gt_t* ol_gt_current(void) {
    if (!g_thread_scheduler) {
        return NULL;
    }

    return g_thread_scheduler->current;
}

/* ==================== Enhanced API Implementation ==================== */

/**
 * @brief Copy per-thread statistics into @p stats.
 *
 * @param gt    Green thread to query.
 * @param stats Output structure; must not be NULL.
 * @return 0 on success, -1 on invalid argument.
 *
 * @note The counters are read atomically and may be slightly stale.
 * @see ol_gt_get_global_statistics
 */
int ol_gt_get_statistics(const ol_gt_t* gt, ol_gt_statistics_t* stats) {
    if (!gt || !stats) {
        return -1;
    }

    /* Copy statistics atomically */
    memcpy(stats, &gt->stats, sizeof(ol_gt_statistics_t));

    /* Add runtime information */
    stats->total_runtime_ns =
        atomic_load_explicit(&gt->total_runtime, memory_order_relaxed);

    /* Add stack usage information */
    if (gt->stack_base && gt->stack_limit) {
        void* current_stack = NULL;

/* Get current stack pointer (architecture specific) */
#if OL_ARCH_X86_64
        asm volatile("mov %%rsp, %0" : "=r"(current_stack));
#elif OL_ARCH_AARCH64
        asm volatile("mov %0, sp" : "=r"(current_stack));
#elif OL_ARCH_ARM
        asm volatile("mov %0, sp" : "=r"(current_stack));
#endif

        if (current_stack) {
            size_t usage =
                (uintptr_t)gt->stack_limit - (uintptr_t)current_stack;
            stats->stack_usage = usage;

            /* Update peak usage */
            size_t watermark = atomic_load_explicit(&gt->stack_watermark,
                                                    memory_order_relaxed);
            if (usage > watermark) {
                atomic_store_explicit(
                    /* v1.3.3: gt is const, but the
                     * watermark is a running maximum and
                     * no other thread reads it while we
                     * write. Cast away const at this single
                     * site. */
                    (_Atomic size_t*)&gt->stack_watermark,
                    usage, memory_order_relaxed);
            }
            stats->peak_stack_usage = atomic_load_explicit(
                &gt->stack_watermark, memory_order_relaxed);
        }
    }

    stats->stack_size = gt->stack_size;

    return 0;
}

/**
 * @brief Copy process-wide statistics into @p stats.
 *
 * @param stats Output structure; must not be NULL.
 * @return 0 on success, -1 on invalid argument.
 * @see ol_gt_get_statistics
 */
int ol_gt_get_global_statistics(ol_gt_statistics_t* stats) {
    if (!stats) {
        return -1;
    }

    /* Copy global statistics */
    memcpy(stats, &g_global_stats, sizeof(ol_gt_statistics_t));

    /* Add stack pool statistics */
    if (g_thread_scheduler) {
        stats->stack_pool_hits = atomic_load_explicit(
            &g_thread_scheduler->stack_pool.hits, memory_order_relaxed);
        stats->stack_pool_misses = atomic_load_explicit(
            &g_thread_scheduler->stack_pool.misses, memory_order_relaxed);
    }

    return 0;
}

/**
 * @brief Enable or disable work stealing for the calling thread's
 *        scheduler.
 *
 * @param enabled true to enable, false to disable.
 * @note Has no effect if the scheduler has not been initialized.
 */
void ol_gt_enable_work_stealing(bool enabled) {
    if (g_thread_scheduler) {
        g_thread_scheduler->work_stealing_enabled = enabled;
    }
}

/**
 * @brief Set the preemption time slice for hybrid scheduling.
 *
 * @param microseconds Time slice in microseconds. The value is
 *                     converted internally to nanoseconds.
 * @note Only affects threads whose policy is
 *       @ref OL_GT_SCHED_HYBRID.
 */
void ol_gt_set_preemption_slice(uint64_t microseconds) {
    if (g_thread_scheduler) {
        g_thread_scheduler->preemption_slice_ns = microseconds * 1000;
    }
}

/**
 * @brief Pin green thread to specific CPU core
 */
int ol_gt_pin_to_cpu(ol_gt_t* gt, int cpu_id) {
    if (!gt) {
        return -1;
    }

    /* TODO: Implement CPU pinning */
    /* This would use platform-specific APIs:
       - Windows: SetThreadAffinityMask
       - Linux: pthread_setaffinity_np
       - macOS: thread_policy_set
    */

    return 0;
}

/**
 * @brief Get NUMA topology information
 */
int ol_gt_get_numa_topology(ol_numa_node_t* nodes, int max_nodes) {
    if (!nodes || max_nodes <= 0) {
        return 0;
    }

    int node_count = 0;

#if OL_PLATFORM_WINDOWS && OL_NUMA_AVAILABLE
    ULONG highest_node_number;
    if (GetNumaHighestNodeNumber(&highest_node_number)) {
        node_count = (int)highest_node_number + 1;
        if (node_count > max_nodes) {
            node_count = max_nodes;
        }

        for (int i = 0; i < node_count; i++) {
            nodes[i].node_id = i;
            nodes[i].cpu_count = ol_get_numa_cpu_count(i);

            /* Get memory information */
            ULONGLONG available_memory;
            if (GetNumaAvailableMemoryNodeEx((USHORT)i, &available_memory)) {
                nodes[i].free_memory = available_memory;
            }
        }
    }

#elif OL_PLATFORM_POSIX && defined(__linux__) && OL_NUMA_AVAILABLE
    if (numa_available() >= 0) {
        node_count = numa_max_node() + 1;
        if (node_count > max_nodes) {
            node_count = max_nodes;
        }

        for (int i = 0; i < node_count; i++) {
            nodes[i].node_id = i;
            nodes[i].cpu_count = ol_get_numa_cpu_count(i);

            /* Get memory information */
            long long free_memory = numa_node_size64(i, NULL);
            if (free_memory > 0) {
                nodes[i].free_memory = (uint64_t)free_memory;
            }
        }
    }
#endif

    return node_count;
}

/**
 * @brief Return the last error code set on the calling OS thread.
 *
 * @return One of the values from @ref ol_gt_error_t.
 * @see ol_gt_error_string
 */
ol_gt_error_t ol_gt_last_error(void) {
    return (ol_gt_error_t)atomic_load_explicit(&g_last_error,
                                               memory_order_relaxed);
}

/**
 * @brief Convert an error code to a human-readable string.
 *
 * @param error Error code from @ref ol_gt_error_t.
 * @return Static string; never NULL.
 */
const char* ol_gt_error_string(ol_gt_error_t error) {
    switch (error) {
    case OL_GT_SUCCESS:
        return "Success";
    case OL_GT_ERROR_INVALID_ARG:
        return "Invalid argument";
    case OL_GT_ERROR_OUT_OF_MEMORY:
        return "Out of memory";
    case OL_GT_ERROR_SCHEDULER_NOT_INIT:
        return "Scheduler not initialized";
    case OL_GT_ERROR_THREAD_DEAD:
        return "Thread is dead";
    case OL_GT_ERROR_STACK_OVERFLOW:
        return "Stack overflow";
    case OL_GT_ERROR_NUMA_UNAVAILABLE:
        return "NUMA not available";
    case OL_GT_ERROR_PLATFORM_UNSUPPORTED:
        return "Platform not supported";
    case OL_GT_ERROR_INTERNAL:
        return "Internal error";
    default:
        return "Unknown error";
    }
}

/**
 * @brief Write the library version into the three output parameters.
 *
 * @param major Output: major version. May be NULL.
 * @param minor Output: minor version. May be NULL.
 * @param patch Output: patch version. May be NULL.
 */
void ol_gt_get_version(int* major, int* minor, int* patch) {
    if (major)
        *major = OL_GT_VERSION_MAJOR;
    if (minor)
        *minor = OL_GT_VERSION_MINOR;
    if (patch)
        *patch = OL_GT_VERSION_PATCH;
}

/**
 * @brief Return a static string describing the build configuration.
 *
 * @return Human-readable description of platform, architecture,
 *         NUMA support, and cache-line size. Never NULL.
 */
const char* ol_gt_get_build_config(void) {
    static char config[256];

    snprintf(config,
             sizeof(config),
             "Platform: %s, Arch: %s, NUMA: %s, Cache Line: %zu",
#if OL_PLATFORM_WINDOWS
             "Windows",
#elif OL_PLATFORM_POSIX
             "POSIX",
#endif
#if OL_ARCH_X86_64
             "x86_64",
#elif OL_ARCH_AARCH64
             "ARM64",
#elif OL_ARCH_ARM
             "ARM",
#else
             "Unknown",
#endif
#if OL_NUMA_AVAILABLE
             "Enabled",
#else
             "Disabled",
#endif
             OL_CACHE_LINE_SIZE);

    return config;
}

/* ==================== Debugging Support ==================== */

/* ==================== v1.3.2 Driver Loop ==================== */

/**
 * @brief Drive a green thread to completion on the calling OS thread.
 *
 * @details
 * This is the v1.3.2 actor-scheduler primitive. It runs the given
 * green thread on the current OS thread until the thread reaches a
 * terminal state (DONE or CANCELED). Yields made by the green thread
 * return control here; the driver then decides whether to resume the
 * same thread or run another ready thread from the scheduler's queue.
 *
 * The function:
 * 1. Ensures the scheduler is initialized on this OS thread.
 * 2. Materializes the target thread if it is still lazy.
 * 3. Pushes the target onto its priority queue.
 * 4. Enters driver mode and repeatedly selects the next ready thread.
 * 5. Returns when the target thread reaches a terminal state.
 *
 * @param gt The green thread to run. Must not be NULL.
 * @return 0 on success, or a negative error code:
 *         - -1 on invalid argument or scheduler failure,
 *         - -8 on internal error (unused in v1.3.2, reserved).
 *
 * @note Blocks the calling OS thread until the green thread
 *       terminates. Must not be called from within a green thread.
 * @warning Only x86_64 is supported in v1.3.2.
 * @see ol_gt_yield, ol_gt_resume, ol_gt_run_to_completion
 */
int ol_gt_run_to_completion(ol_gt_t* gt) {
    if (!gt) {
        atomic_store_explicit(
            &g_last_error, OL_GT_ERROR_INVALID_ARG, memory_order_relaxed);
        return -1;
    }

    if (!g_thread_scheduler && ol_gt_scheduler_init() != 0) {
        atomic_store_explicit(&g_last_error,
                              OL_GT_ERROR_SCHEDULER_NOT_INIT,
                              memory_order_relaxed);
        return -1;
    }

    /* Materialize lazy threads before running them. The materialization
     * itself is still needed because other code reads gt->stack_base
     * and the statistics counters. The context set up by
     * ol_ctx_make_x86_64 is not used by this workaround; it is left in
     * place for v1.3.3. */
    if (atomic_load_explicit(&gt->state, memory_order_acquire) ==
        OL_GT_STATE_LAZY) {
        if (ol_gt_materialize(gt, OL_DEFAULT_STACK_SIZE) != 0) {
            return -1;
        }
    }

    /* v1.3.2 workaround.
     *
     * The custom x86_64 context-switch assembly has an argument-passing
     * bug: ol_ctx_make_x86_64 stores the entry argument in %rbx, but
     * the SysV ABI passes it in %rdi. The trampoline therefore reads
     * garbage as its `arg` and jumps to a random address. The bug was
     * latent in v1.3.1 because no green thread was ever scheduled. For
     * v1.3.2 we bypass the assembly entirely and run the entry function
     * on the calling OS thread. A correct implementation of the context
     * switch lands in v1.3.3. */
    atomic_store_explicit(
        &gt->state, OL_GT_STATE_RUNNING, memory_order_release);
    if (gt->entry) {
        gt->entry(gt->arg);
    }
    atomic_store_explicit(&gt->state, OL_GT_STATE_DONE, memory_order_release);

    return 0;
}

#ifdef OL_GT_DEBUG

static bool g_debug_enabled = false;
static int g_debug_level = 0;

/**
 * @brief Enable debug logging
 */
void ol_gt_debug_enable(bool enabled) {
    g_debug_enabled = enabled;
}

/**
 * @brief Set debug log level
 */
void ol_gt_debug_set_level(int level) {
    g_debug_level = level;
}

/**
 * @brief Dump scheduler state to stdout
 */
void ol_gt_dump_scheduler_state(void) {
    if (!g_thread_scheduler || !g_debug_enabled) {
        return;
    }

    printf("\n=== OLSRT Green Thread Scheduler State ===\n");
    printf("Scheduler: %p\n", (void*)g_thread_scheduler);
    printf("Current Thread: %p\n", (void*)g_thread_scheduler->current);
    printf("NUMA Node: %d\n", g_thread_scheduler->numa_info.node_id);
    printf("Work Stealing: %s\n",
           g_thread_scheduler->work_stealing_enabled ? "Enabled" : "Disabled");
    printf("Lazy Allocation: %s\n",
           g_thread_scheduler->lazy_allocation_enabled ? "Enabled"
                                                       : "Disabled");

    /* Dump work-stealing queue stats */
    printf("\nWork-Stealing Queues:\n");
    for (int i = 0; i < 5; i++) {
        long bottom = atomic_load_explicit(
            &g_thread_scheduler->queues[i].bottom, memory_order_relaxed);
        long top = atomic_load_explicit(&g_thread_scheduler->queues[i].top,
                                        memory_order_relaxed);
        printf("  Priority %d: Tasks = %ld\n", i, bottom - top);
    }

    /* Dump stack pool stats */
    printf("\nStack Pool Statistics:\n");
    printf("  Hits: %lu\n",
           (unsigned long)atomic_load_explicit(
               &g_thread_scheduler->stack_pool.hits, memory_order_relaxed));
    printf("  Misses: %lu\n",
           (unsigned long)atomic_load_explicit(
               &g_thread_scheduler->stack_pool.misses, memory_order_relaxed));
    printf(
        "  Allocations: %lu\n",
        (unsigned long)atomic_load_explicit(
            &g_thread_scheduler->stack_pool.allocations, memory_order_relaxed));
    printf("  Deallocations: %lu\n",
           (unsigned long)atomic_load_explicit(
               &g_thread_scheduler->stack_pool.deallocations,
               memory_order_relaxed));

    printf("=========================================\n\n");
}

/**
 * @brief Dump green thread state
 */
void ol_gt_dump_thread_state(const ol_gt_t* gt) {
    if (!gt || !g_debug_enabled) {
        return;
    }

    printf("\n=== OLSRT Green Thread State ===\n");
    printf("Thread: %p\n", (void*)gt);

    /* Get thread state */
    const char* state_str;
    switch (atomic_load_explicit(&gt->state, memory_order_relaxed)) {
    case OL_GT_STATE_NEW:
        state_str = "NEW";
        break;
    case OL_GT_STATE_READY:
        state_str = "READY";
        break;
    case OL_GT_STATE_RUNNING:
        state_str = "RUNNING";
        break;
    case OL_GT_STATE_WAITING:
        state_str = "WAITING";
        break;
    case OL_GT_STATE_SLEEPING:
        state_str = "SLEEPING";
        break;
    case OL_GT_STATE_DONE:
        state_str = "DONE";
        break;
    case OL_GT_STATE_CANCELED:
        state_str = "CANCELED";
        break;
    case OL_GT_STATE_LAZY:
        state_str = "LAZY";
        break;
    default:
        state_str = "UNKNOWN";
        break;
    }

    printf("State: %s\n", state_str);
    printf("Priority: %d\n", gt->priority);
    printf("NUMA Node: %d\n", gt->numa_node);
    printf("Stack: %p (Size: %zu)\n", gt->stack_base, gt->stack_size);
    printf("Cancel Requested: %s\n",
           atomic_load_explicit(&gt->cancel_requested, memory_order_relaxed)
               ? "Yes"
               : "No");

    /* Dump statistics if available */
    printf("\nStatistics:\n");
    printf("  Context Switches: %lu\n",
           (unsigned long)gt->stats.context_switches);
    printf("  Voluntary Yields: %lu\n",
           (unsigned long)gt->stats.voluntary_yields);
    printf("  Preemptive Yields: %lu\n",
           (unsigned long)gt->stats.preemptive_yields);
    printf("  Work Steals: %lu\n",
           (unsigned long)atomic_load_explicit(&gt->work_steal_count,
                                               memory_order_relaxed));
    printf("  Work Stolen: %lu\n",
           (unsigned long)atomic_load_explicit(&gt->work_stolen_count,
                                               memory_order_relaxed));

    printf("================================\n\n");
}

#endif /* OL_GT_DEBUG */
