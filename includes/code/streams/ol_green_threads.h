/**
 * @file ol_green_threads.h
 * @brief Advanced Cross-Platform Green Threads & Fibers Implementation
 * @version 1.3.0
 * @date 2026
 * 
 * @note This is the heart of OLSRT Modern Concurrency
 * @warning API signatures MUST NOT be changed - Only internal logic can be modified
 */

#ifndef OL_GREEN_THREADS_H
#define OL_GREEN_THREADS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== System Headers ==================== */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

/* ==================== Platform Detection ==================== */
#if defined(_WIN32) || defined(_WIN64)
    #define OL_PLATFORM_WINDOWS 1
    #define OL_PLATFORM_POSIX 0
#elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
      defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    #define OL_PLATFORM_WINDOWS 0
    #define OL_PLATFORM_POSIX 1
#else
    #error "OLSRT: Unsupported platform"
#endif

/* ==================== Architecture Detection ==================== */
#if defined(__x86_64__) || defined(_M_X64)
    #define OL_ARCH_X86_64 1
    #define OL_ARCH_AARCH64 0
    #define OL_ARCH_ARM 0
#elif defined(__aarch64__) || defined(__arm64__)
    #define OL_ARCH_X86_64 0
    #define OL_ARCH_AARCH64 1
    #define OL_ARCH_ARM 0
#elif defined(__arm__) || defined(__thumb__)
    #define OL_ARCH_X86_64 0
    #define OL_ARCH_AARCH64 0
    #define OL_ARCH_ARM 1
#else
    #define OL_ARCH_X86_64 0
    #define OL_ARCH_AARCH64 0
    #define OL_ARCH_ARM 0
#endif

/* ==================== Compiler Features ==================== */
#if defined(__GNUC__) || defined(__clang__)
    #define OL_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define OL_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #define OL_ALWAYS_INLINE __attribute__((always_inline)) inline
    #define OL_NOINLINE __attribute__((noinline))
    #define OL_ALIGNED(x) __attribute__((aligned(x)))
    #define OL_PACKED __attribute__((packed))
    #define OL_COLD __attribute__((cold))
    #define OL_HOT __attribute__((hot))
#elif defined(_MSC_VER)
    #define OL_LIKELY(x)   (x)
    #define OL_UNLIKELY(x) (x)
    #define OL_ALWAYS_INLINE __forceinline
    #define OL_NOINLINE __declspec(noinline)
    #define OL_ALIGNED(x) __declspec(align(x))
    #define OL_PACKED
    #define OL_COLD
    #define OL_HOT
#else
    #define OL_LIKELY(x)   (x)
    #define OL_UNLIKELY(x) (x)
    #define OL_ALWAYS_INLINE static inline
    #define OL_NOINLINE
    #define OL_ALIGNED(x)
    #define OL_PACKED
    #define OL_COLD
    #define OL_HOT
#endif

/* ==================== Atomic Operations ==================== */
#if defined(__STDC_NO_ATOMICS__)
    #error "OLSRT requires C11 atomics support"
#else
    #include <stdatomic.h>
#endif

/* ==================== Core Types ==================== */

/**
 * @brief Green Thread state enumeration
 */
typedef enum {
    OL_GT_STATE_NEW        = 0,  /**< Thread created but not initialized */
    OL_GT_STATE_READY      = 1,  /**< Ready to run */
    OL_GT_STATE_RUNNING    = 2,  /**< Currently executing */
    OL_GT_STATE_WAITING    = 3,  /**< Waiting for I/O or event */
    OL_GT_STATE_SLEEPING   = 4,  /**< Sleeping (timer) */
    OL_GT_STATE_DONE       = 5,  /**< Execution completed */
    OL_GT_STATE_CANCELED   = 6,  /**< Canceled by user */
    OL_GT_STATE_LAZY       = 7   /**< Lazy allocated - only metadata */
} ol_gt_state_t;

/**
 * @brief Thread priority levels
 */
typedef enum {
    OL_GT_PRIORITY_IDLE     = 0,  /**< Only run when nothing else */
    OL_GT_PRIORITY_LOW      = 1,  /**< Background tasks */
    OL_GT_PRIORITY_NORMAL   = 2,  /**< Default priority */
    OL_GT_PRIORITY_HIGH     = 3,  /**< Interactive tasks */
    OL_GT_PRIORITY_REALTIME = 4   /**< Time-critical tasks */
} ol_gt_priority_t;

/**
 * @brief Scheduling policies
 */
typedef enum {
    OL_GT_SCHED_COOPERATIVE = 0,  /**< Only yield voluntarily */
    OL_GT_SCHED_PREEMPTIVE  = 1,  /**< Time-sliced preemption */
    OL_GT_SCHED_HYBRID      = 2   /**< Adaptive hybrid scheduling */
} ol_gt_sched_policy_t;

/**
 * @brief NUMA node information
 */
typedef struct {
    int32_t node_id;           /**< NUMA node ID (-1 if unknown) */
    uint32_t cpu_count;        /**< CPUs in this node */
    uint64_t free_memory;      /**< Free memory in bytes */
    uint8_t distance[16];      /**< Distance to other nodes */
} ol_numa_node_t;

/**
 * @brief Green Thread entry function type
 * @param arg User-provided argument
 */
typedef void (*ol_gt_entry_fn)(void* arg);

/* ==================== Forward Declarations ==================== */
typedef struct ol_gt ol_gt_t;
typedef struct ol_gt_scheduler ol_gt_scheduler_t;
typedef struct ol_work_stealing_queue ol_work_stealing_queue_t;
typedef struct ol_stack_pool ol_stack_pool_t;
typedef struct ol_gt_statistics ol_gt_statistics_t;

/* ==================== ORIGINAL API - MUST NOT CHANGE ==================== */

/**
 * @brief Initialize the green thread scheduler
 * @return 0 on success, -1 on error
 * @note Original API - Signature must not change
 */
int ol_gt_scheduler_init(void);

/**
 * @brief Shutdown the green thread scheduler
 * @note Original API - Signature must not change
 */
void ol_gt_scheduler_shutdown(void);

/**
 * @brief Create a new green thread
 * @param entry Entry point function
 * @param arg Argument to pass to entry function
 * @param stack_size Stack size in bytes (0 for default)
 * @return Pointer to green thread or NULL on error
 * @note Original API - Signature must not change
 */
ol_gt_t* ol_gt_spawn(ol_gt_entry_fn entry, void* arg, size_t stack_size);

/**
 * @brief Resume execution of a green thread
 * @param gt Green thread to resume
 * @return 0 on success, -1 on error
 * @note Original API - Signature must not change
 */
int ol_gt_resume(ol_gt_t* gt);

/**
 * @brief Yield execution to another green thread
 * @note Original API - Signature must not change
 */
void ol_gt_yield(void);

/**
 * @brief Wait for a green thread to complete
 * @param gt Green thread to join
 * @return 0 on success, -1 on error
 * @note Original API - Signature must not change
 */
int ol_gt_join(ol_gt_t* gt);

/**
 * @brief Destroy a green thread
 * @param gt Green thread to destroy
 * @note Original API - Signature must not change
 */
void ol_gt_destroy(ol_gt_t* gt);

/**
 * @brief Cancel a green thread
 * @param gt Green thread to cancel
 * @return 0 on success, -1 on error
 * @note Original API - Signature must not change
 */
int ol_gt_cancel(ol_gt_t* gt);

/**
 * @brief Check if a green thread is alive
 * @param gt Green thread to check
 * @return true if alive, false otherwise
 * @note Original API - Signature must not change
 */
bool ol_gt_is_alive(const ol_gt_t* gt);

/**
 * @brief Check if a green thread is canceled
 * @param gt Green thread to check
 * @return true if canceled, false otherwise
 * @note Original API - Signature must not change
 */
bool ol_gt_is_canceled(const ol_gt_t* gt);

/**
 * @brief Get current running green thread
 * @return Current green thread or NULL
 * @note Original API - Signature must not change
 */
ol_gt_t* ol_gt_current(void);

/* ==================== Enhanced Internal API ==================== */

/**
 * @brief Advanced green thread configuration
 */
typedef struct {
    ol_gt_priority_t priority;          /**< Thread priority */
    ol_gt_sched_policy_t sched_policy;  /**< Scheduling policy */
    size_t stack_size;                  /**< Custom stack size (0 = default) */
    int32_t numa_node;                  /**< Preferred NUMA node (-1 = any) */
    bool lazy_allocation;               /**< Create lazily */
    bool enable_stats;                  /**< Collect statistics */
    uint8_t reserved[32];               /**< Reserved for future use */
} ol_gt_config_t;

/**
 * @brief Create green thread with advanced configuration
 * @param entry Entry point function
 * @param arg Argument to pass
 * @param config Configuration structure
 * @return Pointer to green thread or NULL
 * @internal Internal API only
 */
ol_gt_t* ol_gt_spawn_ex(ol_gt_entry_fn entry, void* arg, const ol_gt_config_t* config);

/**
 * @brief Get green thread statistics
 * @param gt Green thread
 * @param stats Output statistics
 * @return 0 on success, -1 on error
 * @internal Internal API only
 */
int ol_gt_get_statistics(const ol_gt_t* gt, ol_gt_statistics_t* stats);

/**
 * @brief Get global scheduler statistics
 * @param stats Output statistics
 * @return 0 on success, -1 on error
 * @internal Internal API only
 */
int ol_gt_get_global_statistics(ol_gt_statistics_t* stats);

/**
 * @brief Enable work stealing for current scheduler
 * @param enabled true to enable, false to disable
 * @internal Internal API only
 */
void ol_gt_enable_work_stealing(bool enabled);

/**
 * @brief Set preemption time slice
 * @param microseconds Time slice in microseconds
 * @internal Internal API only
 */
void ol_gt_set_preemption_slice(uint64_t microseconds);

/**
 * @brief Pin green thread to specific CPU core
 * @param gt Green thread
 * @param cpu_id CPU core ID
 * @return 0 on success, -1 on error
 * @internal Internal API only
 */
int ol_gt_pin_to_cpu(ol_gt_t* gt, int cpu_id);

/**
 * @brief Get NUMA topology information
 * @param nodes Output array for NUMA nodes
 * @param max_nodes Maximum nodes to retrieve
 * @return Number of nodes found
 * @internal Internal API only
 */
int ol_gt_get_numa_topology(ol_numa_node_t* nodes, int max_nodes);

/* ==================== Statistics Structure ==================== */

/**
 * @brief Comprehensive green thread statistics
 */
typedef struct ol_gt_statistics {
    /* Thread-specific stats */
    uint64_t spawn_count;           /**< Number of times spawned */
    uint64_t destroy_count;         /**< Number of times destroyed */
    uint64_t context_switches;      /**< Context switches */
    uint64_t voluntary_yields;      /**< Voluntary yields */
    uint64_t preemptive_yields;     /**< Preemptive yields */
    uint64_t work_steals;           /**< Work steals performed */
    uint64_t work_stolen;           /**< Work stolen from others */
    uint64_t cpu_migrations;        /**< CPU migrations */
    uint64_t numa_migrations;       /**< NUMA node migrations */
    
    /* Timing statistics */
    uint64_t total_runtime_ns;      /**< Total runtime in nanoseconds */
    uint64_t avg_runtime_ns;        /**< Average runtime */
    uint64_t max_runtime_ns;        /**< Maximum runtime */
    uint64_t min_runtime_ns;        /**< Minimum runtime */
    
    /* Memory statistics */
    size_t stack_usage;             /**< Current stack usage */
    size_t peak_stack_usage;        /**< Peak stack usage */
    size_t stack_size;              /**< Allocated stack size */
    uint64_t stack_pool_hits;       /**< Stack pool cache hits */
    uint64_t stack_pool_misses;     /**< Stack pool cache misses */
    
    /* Scheduling statistics */
    uint64_t priority_changes;      /**< Priority changes */
    uint64_t scheduler_preemptions; /**< Scheduler preemptions */
    uint64_t wait_time_ns;          /**< Time spent waiting */
    uint64_t ready_time_ns;         /**< Time spent ready */
    uint64_t running_time_ns;       /**< Time spent running */
    
    /* Error statistics */
    uint64_t cancellation_requests; /**< Cancellation requests */
    uint64_t allocation_failures;   /**< Memory allocation failures */
    uint64_t stack_overflows;       /**< Stack overflow detections */
    uint64_t invalid_operations;    /**< Invalid operations */
    
    /* NUMA statistics */
    int32_t current_numa_node;      /**< Current NUMA node */
    uint64_t numa_local_accesses;   /**< Local NUMA accesses */
    uint64_t numa_remote_accesses;  /**< Remote NUMA accesses */
    
    uint8_t reserved[128];          /**< Reserved for future expansion */
} ol_gt_statistics_t;

/* ==================== Internal Structures (Opaque) ==================== */

/** @cond INTERNAL */

/* Forward declarations for internal structures */
struct ol_gt {
    /* Basic metadata (192 bytes) */
    alignas(64) atomic_uint_fast32_t state;
    ol_gt_entry_fn entry;
    void* arg;
    
    /* Context switching */
#if OL_PLATFORM_WINDOWS
    void* fiber;
#else
    struct {
        void* stack_ptr;
        void* instruction_ptr;
        uintptr_t registers[16];
    } context;
#endif
    
    /* Stack management */
    void* stack_base;
    void* stack_limit;
    size_t stack_size;
    atomic_size_t stack_watermark;
    
    /* Scheduling */
    ol_gt_priority_t priority;
    ol_gt_sched_policy_t sched_policy;
    atomic_int_fast64_t last_run_time;
    atomic_int_fast64_t total_runtime;
    
    /* Work stealing */
    atomic_uint_fast32_t work_steal_count;
    atomic_uint_fast32_t work_stolen_count;
    
    /* NUMA awareness */
    int32_t numa_node;
    atomic_uint_fast32_t numa_local_accesses;
    atomic_uint_fast32_t numa_remote_accesses;
    
    /* Statistics */
    ol_gt_statistics_t stats;
    
    /* Linked list for scheduler */
    struct ol_gt* next;
    struct ol_gt* prev;
    
    /* Cancellation */
    atomic_bool cancel_requested;
    atomic_bool cancel_flag;
    
    /* Padding to 1024 bytes exactly */
    uint8_t padding[1024 - 384];
} OL_ALIGNED(64) OL_PACKED;

struct ol_work_stealing_queue {
    /* Chase-Lev work-stealing deque */
    atomic_uintptr_t* tasks;
    atomic_long bottom;
    atomic_long top;
    size_t capacity;
    size_t mask;
};

struct ol_stack_pool {
    /* Segregated stack pool by size */
    struct {
        void** stacks;
        atomic_size_t count;
        size_t size;
    } buckets[8]; /* 1KB, 2KB, 4KB, 8KB, 16KB, 32KB, 64KB, 128KB */
    
    atomic_uint_fast64_t hits;
    atomic_uint_fast64_t misses;
    atomic_uint_fast64_t allocations;
    atomic_uint_fast64_t deallocations;
};

struct ol_gt_scheduler {
    /* Per-thread scheduler instance */
    
    /* Work stealing queues per priority */
    ol_work_stealing_queue_t queues[5]; /* One per priority level */
    
    /* Current running thread */
    ol_gt_t* current;
    
    /* Statistics */
    ol_gt_statistics_t global_stats;
    
    /* Stack pool */
    ol_stack_pool_t stack_pool;
    
    /* NUMA information */
    ol_numa_node_t numa_info;
    int32_t current_cpu;
    
    /* Preemption timer */
    uint64_t preemption_slice_ns;
    atomic_uint_fast64_t last_preemption;
    
    /* Configuration */
    bool work_stealing_enabled;
    bool lazy_allocation_enabled;
    bool numa_awareness_enabled;
    bool statistics_enabled;
    
    /* Thread-local storage */
    alignas(64) uint8_t tls[256];
};

/** @endcond */

/* ==================== Compiler-specific Optimizations ==================== */

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    #define OL_PAUSE() __builtin_ia32_pause()
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    #define OL_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(__arm__)
    #define OL_PAUSE() asm volatile("yield" ::: "memory")
#else
    #define OL_PAUSE() ((void)0)
#endif

/* Branch prediction hints */
#if defined(__GNUC__) || defined(__clang__)
    #define OL_EXPECT_TRUE(x)  __builtin_expect(!!(x), 1)
    #define OL_EXPECT_FALSE(x) __builtin_expect(!!(x), 0)
#else
    #define OL_EXPECT_TRUE(x)  (x)
    #define OL_EXPECT_FALSE(x) (x)
#endif

/* Cache line size detection */
#ifndef OL_CACHE_LINE_SIZE
    #if defined(__x86_64__) || defined(__i386__)
        #define OL_CACHE_LINE_SIZE 64
    #elif defined(__aarch64__)
        #define OL_CACHE_LINE_SIZE 64
    #elif defined(__arm__)
        #define OL_CACHE_LINE_SIZE 32
    #else
        #define OL_CACHE_LINE_SIZE 64
    #endif
#endif

/* Force inlining of critical functions */
#define OL_CRITICAL_SECTION_BEGIN() \
    do { \
        _Pragma("GCC diagnostic push") \
        _Pragma("GCC diagnostic ignored \"-Wignored-optimization-argument\"") \
        asm volatile("" ::: "memory"); \
        _Pragma("GCC diagnostic pop") \
    } while(0)

#define OL_CRITICAL_SECTION_END() \
    asm volatile("" ::: "memory")

/* ==================== Platform-specific Declarations ==================== */

#if OL_PLATFORM_WINDOWS
    #include <windows.h>
    #include <fibersapi.h>
    
    /* Windows-specific fiber context */
    typedef struct {
        LPVOID fiber;
        DWORD thread_id;
        BOOL is_fiber;
    } ol_win_context_t;
    
#elif OL_PLATFORM_POSIX
    #include <unistd.h>
    #include <sys/mman.h>
    #include <sys/syscall.h>
    #include <pthread.h>
    #include <sched.h>
    
    #if defined(__linux__)
        #include <sys/timerfd.h>
        #include <sys/eventfd.h>
        #include <linux/futex.h>
    #endif
    
    /* POSIX-specific context for assembly switching */
    typedef struct {
        void* stack_ptr;
        void* instruction_ptr;
        uintptr_t registers[16];
        uint32_t flags;
    } ol_posix_context_t;
    
#endif

/* ==================== Assembly Context Switching ==================== */

/**
 * @brief Save current execution context
 * @param ctx Context structure to save into
 * @note Platform-specific assembly implementation
 */
OL_ALWAYS_INLINE void ol_ctx_save(void* ctx);

/**
 * @brief Restore execution context
 * @param ctx Context structure to restore from
 * @note Platform-specific assembly implementation
 */
OL_ALWAYS_INLINE void ol_ctx_restore(const void* ctx);

/**
 * @brief Initialize context for new green thread
 * @param ctx Context to initialize
 * @param entry Entry point function
 * @param arg Argument to pass
 * @param stack_base Stack base address
 * @param stack_size Stack size
 * @note Platform-specific assembly implementation
 */
OL_ALWAYS_INLINE void ol_ctx_make(void* ctx, ol_gt_entry_fn entry, void* arg,
                                  void* stack_base, size_t stack_size);

/* ==================== Memory Management ==================== */

/**
 * @brief Allocate aligned memory with NUMA awareness
 * @param size Size in bytes
 * @param alignment Alignment requirement
 * @param numa_node Preferred NUMA node (-1 for any)
 * @return Pointer to allocated memory or NULL
 */
void* ol_numa_alloc(size_t size, size_t alignment, int numa_node);

/**
 * @brief Free NUMA-aware memory
 * @param ptr Pointer to memory
 * @param size Size in bytes
 */
void ol_numa_free(void* ptr, size_t size);

/**
 * @brief Get current NUMA node
 * @return NUMA node ID or -1 if unknown
 */
int ol_get_current_numa_node(void);

/**
 * @brief Get CPU core count per NUMA node
 * @param numa_node NUMA node ID
 * @return Number of CPU cores or -1 on error
 */
int ol_get_numa_cpu_count(int numa_node);

/* ==================== Error Handling ==================== */

/**
 * @brief Green thread error codes
 */
typedef enum {
    OL_GT_SUCCESS = 0,
    OL_GT_ERROR_INVALID_ARG = -1,
    OL_GT_ERROR_OUT_OF_MEMORY = -2,
    OL_GT_ERROR_SCHEDULER_NOT_INIT = -3,
    OL_GT_ERROR_THREAD_DEAD = -4,
    OL_GT_ERROR_STACK_OVERFLOW = -5,
    OL_GT_ERROR_NUMA_UNAVAILABLE = -6,
    OL_GT_ERROR_PLATFORM_UNSUPPORTED = -7,
    OL_GT_ERROR_INTERNAL = -8
} ol_gt_error_t;

/**
 * @brief Get last error code
 * @return Last error code
 */
ol_gt_error_t ol_gt_last_error(void);

/**
 * @brief Get error string
 * @param error Error code
 * @return Error description
 */
const char* ol_gt_error_string(ol_gt_error_t error);

/* ==================== Debugging & Profiling ==================== */

#ifdef OL_GT_DEBUG
    /**
     * @brief Enable debug logging
     * @param enabled true to enable, false to disable
     */
    void ol_gt_debug_enable(bool enabled);
    
    /**
     * @brief Set debug log level
     * @param level Log level (0=error, 1=warning, 2=info, 3=debug)
     */
    void ol_gt_debug_set_level(int level);
    
    /**
     * @brief Dump scheduler state to stdout
     */
    void ol_gt_dump_scheduler_state(void);
    
    /**
     * @brief Dump green thread state
     * @param gt Green thread to dump
     */
    void ol_gt_dump_thread_state(const ol_gt_t* gt);
#endif

/* ==================== Internal Optimization Macros ==================== */

/* Avoid false sharing */
#define OL_CACHE_ALIGN alignas(OL_CACHE_LINE_SIZE)

/* Compiler barrier */
#define OL_COMPILER_BARRIER() asm volatile("" ::: "memory")

/* Memory barrier */
#if defined(__x86_64__) || defined(__i386__)
    #define OL_MEMORY_BARRIER() asm volatile("mfence" ::: "memory")
#elif defined(__aarch64__)
    #define OL_MEMORY_BARRIER() asm volatile("dmb ish" ::: "memory")
#elif defined(__arm__)
    #define OL_MEMORY_BARRIER() asm volatile("dmb" ::: "memory")
#else
    #define OL_MEMORY_BARRIER() __sync_synchronize()
#endif

/* Atomic load with acquire semantics */
#define OL_ATOMIC_LOAD_ACQUIRE(ptr) \
    atomic_load_explicit(ptr, memory_order_acquire)

/* Atomic store with release semantics */
#define OL_ATOMIC_STORE_RELEASE(ptr, val) \
    atomic_store_explicit(ptr, val, memory_order_release)

/* Compare and exchange */
#define OL_ATOMIC_CAS(ptr, expected, desired) \
    atomic_compare_exchange_strong_explicit( \
        ptr, expected, desired, \
        memory_order_acq_rel, memory_order_acquire)

/* ==================== Performance Counters ==================== */

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    static inline uint64_t ol_rdtsc(void) {
        uint32_t lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return ((uint64_t)hi << 32) | lo;
    }
    #define OL_PERF_COUNTER_AVAILABLE 1
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    #include <intrin.h>
    #pragma intrinsic(__rdtsc)
    static inline uint64_t ol_rdtsc(void) { return __rdtsc(); }
    #define OL_PERF_COUNTER_AVAILABLE 1
#else
    static inline uint64_t ol_rdtsc(void) { return 0; }
    #define OL_PERF_COUNTER_AVAILABLE 0
#endif

/* ==================== API Version Information ==================== */

#define OL_GT_VERSION_MAJOR 1
#define OL_GT_VERSION_MINOR 3
#define OL_GT_VERSION_PATCH 0
#define OL_GT_VERSION_STRING "1.3.0"

/**
 * @brief Get library version
 * @param major Major version output
 * @param minor Minor version output
 * @param patch Patch version output
 */
void ol_gt_get_version(int* major, int* minor, int* patch);

/**
 * @brief Get build configuration
 * @return Build configuration string
 */
const char* ol_gt_get_build_config(void);

#ifdef __cplusplus
}
#endif

#endif /* OL_GREEN_THREADS_H */
