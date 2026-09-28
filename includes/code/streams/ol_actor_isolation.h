/**
 * @file ol_actor_isolation.h
 * @brief Complete memory isolation and arena management with architecture-specific optimizations
 * @version 3.0.0
 * 
 * @details This header provides cross-platform memory isolation with zero-copy operations
 * and architecture-specific inline assembly optimizations for x86, x86_64, AMD64, RISC-V, ARM64, ARM.
 * Each actor gets its own isolated memory arena with guard pages and efficient allocation.
 * 
 * Key features:
 * - Zero-copy inter-process communication using shared memory
 * - Guard pages for buffer overflow detection
 * - Architecture-specific memory barriers and atomic operations
 * - SIMD-optimized memory operations when available
 * - Memory arenas with lock-free allocation
 * 
 * @author OverLab Group
 * @date 2026
 * 
 * 
 * 
 */

#ifndef OL_ACTOR_ISOLATION_H
#define OL_ACTOR_ISOLATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdalign.h>

/* ==================== Platform Detection ==================== */

#if defined(_WIN32) || defined(_WIN64)
    #define OL_PLATFORM_WINDOWS 1
    #define OL_PLATFORM_POSIX 0
    #include <windows.h>
    #include <intrin.h>
    #pragma intrinsic(_ReadWriteBarrier)
    #pragma intrinsic(_mm_mfence)
    #pragma intrinsic(_mm_lfence)
    #pragma intrinsic(_mm_sfence)
#elif defined(__APPLE__) && defined(__MACH__)
    #define OL_PLATFORM_MACOS 1
    #define OL_PLATFORM_POSIX 1
    #include <TargetConditionals.h>
    #include <libkern/OSAtomic.h>
    #include <mach/mach.h>
    #include <mach/vm_map.h>
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    #define OL_PLATFORM_LINUX 1
    #define OL_PLATFORM_POSIX 1
    #include <unistd.h>
    #include <sys/mman.h>
    #include <sched.h>
#else
    #error "Unsupported platform"
#endif

/* ==================== Architecture Detection ==================== */

#if defined(__x86_64__) || defined(_M_X64)
    #define OL_ARCH_X86_64 1
    #define OL_ARCH_64BIT 1
    #include <xmmintrin.h>
    #include <emmintrin.h>
    #if defined(__AVX512F__)
        #define OL_ARCH_AVX512 1
        #include <immintrin.h>
    #elif defined(__AVX2__)
        #define OL_ARCH_AVX2 1
        #include <immintrin.h>
    #elif defined(__AVX__)
        #define OL_ARCH_AVX 1
        #include <immintrin.h>
    #endif
#elif defined(__i386__) || defined(_M_IX86)
    #define OL_ARCH_X86 1
    #define OL_ARCH_32BIT 1
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define OL_ARCH_ARM64 1
    #define OL_ARCH_64BIT 1
    #include <arm_acle.h>
    #include <arm_neon.h>
#elif defined(__arm__) || defined(_M_ARM)
    #define OL_ARCH_ARM 1
    #define OL_ARCH_32BIT 1
    #if defined(__ARM_NEON__)
        #define OL_ARCH_NEON 1
        #include <arm_neon.h>
    #endif
#elif defined(__riscv) && (__riscv_xlen == 64)
    #define OL_ARCH_RISCV64 1
    #define OL_ARCH_64BIT 1
#elif defined(__riscv) && (__riscv_xlen == 32)
    #define OL_ARCH_RISCV32 1
    #define OL_ARCH_32BIT 1
#else
    #error "Unsupported architecture"
#endif

/* ==================== Compiler-Specific Macros ==================== */

#if defined(__GNUC__) || defined(__clang__)
    #define OL_LIKELY(x)       __builtin_expect(!!(x), 1)
    #define OL_UNLIKELY(x)     __builtin_expect(!!(x), 0)
    #define OL_NOINLINE        __attribute__((noinline))
    #define OL_ALWAYS_INLINE   __attribute__((always_inline))
    #define OL_FLATTEN         __attribute__((flatten))
    #define OL_PACKED          __attribute__((packed))
    #define OL_ALIGNED(x)      __attribute__((aligned(x)))
    #define OL_COLD            __attribute__((cold))
    #define OL_HOT             __attribute__((hot))
    #define OL_PURE            __attribute__((pure))
    #define OL_CONST           __attribute__((const))
    #define OL_RESTRICT        __restrict__
    #define OL_ASSUME_ALIGNED(ptr, alignment) ((typeof(ptr))__builtin_assume_aligned((ptr), (alignment)))
#elif defined(_MSC_VER)
    #define OL_LIKELY(x)       (x)
    #define OL_UNLIKELY(x)     (x)
    #define OL_NOINLINE        __declspec(noinline)
    #define OL_ALWAYS_INLINE   __forceinline
    #define OL_FLATTEN         
    #define OL_PACKED          
    #define OL_ALIGNED(x)      __declspec(align(x))
    #define OL_COLD            
    #define OL_HOT             
    #define OL_PURE            __declspec(noalias)
    #define OL_CONST           __declspec(noalias) __declspec(const)
    #define OL_RESTRICT        __restrict
    #define OL_ASSUME_ALIGNED(ptr, alignment) __assume(((uintptr_t)(ptr) & ((alignment) - 1)) == 0); (ptr)
#else
    #define OL_LIKELY(x)       (x)
    #define OL_UNLIKELY(x)     (x)
    #define OL_NOINLINE        
    #define OL_ALWAYS_INLINE   inline
    #define OL_FLATTEN         
    #define OL_PACKED          
    #define OL_ALIGNED(x)      
    #define OL_COLD            
    #define OL_HOT             
    #define OL_PURE            
    #define OL_CONST           
    #define OL_RESTRICT        
    #define OL_ASSUME_ALIGNED(ptr, alignment) (ptr)
#endif

/* ==================== Architecture-Specific Inline Assembly ==================== */

/**
 * @def OL_MEMORY_BARRIER
 * @brief Full memory barrier (load/store)
 * @details Ensures all memory operations before the barrier are visible to all processors
 *          before any operations after the barrier.
 */
#if OL_ARCH_X86 || OL_ARCH_X86_64
    #define OL_MEMORY_BARRIER() __asm__ volatile("mfence" ::: "memory")
#elif OL_ARCH_ARM64
    #define OL_MEMORY_BARRIER() __asm__ volatile("dmb ish" ::: "memory")
#elif OL_ARCH_ARM
    #define OL_MEMORY_BARRIER() __asm__ volatile("dmb" ::: "memory")
#elif OL_ARCH_RISCV64 || OL_ARCH_RISCV32
    #define OL_MEMORY_BARRIER() __asm__ volatile("fence iorw, iorw" ::: "memory")
#elif OL_PLATFORM_WINDOWS
    #define OL_MEMORY_BARRIER() _mm_mfence()
#else
    #define OL_MEMORY_BARRIER() __sync_synchronize()
#endif

/**
 * @def OL_COMPILER_BARRIER
 * @brief Compiler-only memory barrier
 * @details Prevents compiler reordering but doesn't affect CPU memory ordering
 */
#if defined(__GNUC__) || defined(__clang__)
    #define OL_COMPILER_BARRIER() __asm__ volatile("" ::: "memory")
#elif defined(_MSC_VER)
    #define OL_COMPILER_BARRIER() _ReadWriteBarrier()
#else
    #define OL_COMPILER_BARRIER() 
#endif

/**
 * @def OL_PREFETCH_READ
 * @brief Prefetch memory for reading
 * @param ptr Pointer to memory location
 * @details Hints the processor to load data into cache before it's needed
 */
#if OL_ARCH_X86 || OL_ARCH_X86_64
    #define OL_PREFETCH_READ(ptr) __builtin_prefetch((ptr), 0, 3)
#elif OL_ARCH_ARM64 || OL_ARCH_ARM
    #define OL_PREFETCH_READ(ptr) __asm__ volatile("prfm pldl1keep, [%0]" : : "r"(ptr) :)
#else
    #define OL_PREFETCH_READ(ptr) __builtin_prefetch((ptr), 0, 3)
#endif

/**
 * @def OL_PREFETCH_WRITE
 * @brief Prefetch memory for writing
 * @param ptr Pointer to memory location
 */
#if OL_ARCH_X86 || OL_ARCH_X86_64
    #define OL_PREFETCH_WRITE(ptr) __builtin_prefetch((ptr), 1, 3)
#elif OL_ARCH_ARM64 || OL_ARCH_ARM
    #define OL_PREFETCH_WRITE(ptr) __asm__ volatile("prfm pstl1keep, [%0]" : : "r"(ptr) :)
#else
    #define OL_PREFETCH_WRITE(ptr) __builtin_prefetch((ptr), 1, 3)
#endif

/**
 * @def OL_ATOMIC_LOAD_ACQUIRE
 * @brief Atomic load with acquire semantics
 * @param ptr Pointer to atomic variable
 * @return Value loaded
 */
#if defined(__GNUC__) || defined(__clang__)
    #define OL_ATOMIC_LOAD_ACQUIRE(ptr) __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
#elif defined(_MSC_VER)
    #define OL_ATOMIC_LOAD_ACQUIRE(ptr) (OL_MEMORY_BARRIER(), *(ptr))
#else
    #define OL_ATOMIC_LOAD_ACQUIRE(ptr) (*(volatile typeof(*(ptr))*)(ptr))
#endif

/**
 * @def OL_ATOMIC_STORE_RELEASE
 * @brief Atomic store with release semantics
 * @param ptr Pointer to atomic variable
 * @param val Value to store
 */
#if defined(__GNUC__) || defined(__clang__)
    #define OL_ATOMIC_STORE_RELEASE(ptr, val) __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
#elif defined(_MSC_VER)
    #define OL_ATOMIC_STORE_RELEASE(ptr, val) (*(ptr) = (val), OL_MEMORY_BARRIER())
#else
    #define OL_ATOMIC_STORE_RELEASE(ptr, val) (*(volatile typeof(*(ptr))*)(ptr) = (val))
#endif

/**
 * @def OL_CACHE_LINE_SIZE
 * @brief Size of CPU cache line in bytes
 */
#if OL_ARCH_X86_64 || OL_ARCH_X86
    #define OL_CACHE_LINE_SIZE 64
#elif OL_ARCH_ARM64
    #define OL_CACHE_LINE_SIZE 64
#elif OL_ARCH_ARM
    #define OL_CACHE_LINE_SIZE 32
#elif OL_ARCH_RISCV64 || OL_ARCH_RISCV32
    #define OL_CACHE_LINE_SIZE 64
#else
    #define OL_CACHE_LINE_SIZE 64
#endif

/**
 * @def OL_PAGE_SIZE
 * @brief System page size in bytes
 */
static inline size_t ol_get_page_size(void) OL_PURE;
static inline size_t ol_get_page_size(void) {
#if OL_PLATFORM_POSIX
    static size_t page_size = 0;
    if (OL_UNLIKELY(page_size == 0)) {
        page_size = sysconf(_SC_PAGESIZE);
    }
    return page_size;
#elif OL_PLATFORM_WINDOWS
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    return sys_info.dwPageSize;
#else
    return 4096;
#endif
}

#define OL_PAGE_SIZE ol_get_page_size()

/* ==================== SIMD-Optimized Memory Operations ==================== */

/**
 * @brief Fast memory copy with SIMD acceleration
 * @param dest Destination buffer (must be 16-byte aligned)
 * @param src Source buffer (must be 16-byte aligned)
 * @param n Number of bytes to copy (multiple of 64)
 * @return dest
 * 
 * @note Uses AVX-512, AVX2, AVX, SSE, or NEON depending on architecture
 */
void* ol_memcpy_fast(void* OL_RESTRICT dest, const void* OL_RESTRICT src, size_t n) OL_HOT;

/**
 * @brief Fast memory set with SIMD acceleration
 * @param dest Destination buffer (must be 16-byte aligned)
 * @param c Value to set (converted to unsigned char)
 * @param n Number of bytes to set (multiple of 64)
 * @return dest
 */
void* ol_memset_fast(void* dest, int c, size_t n) OL_HOT;

/**
 * @brief Fast memory compare with SIMD acceleration
 * @param s1 First buffer (must be 16-byte aligned)
 * @param s2 Second buffer (must be 16-byte aligned)
 * @param n Number of bytes to compare (multiple of 64)
 * @return 0 if equal, non-zero otherwise
 */
int ol_memcmp_fast(const void* s1, const void* s2, size_t n) OL_PURE OL_HOT;

/* ==================== Arena Management Structures ==================== */

/**
 * @brief Memory allocation statistics
 */
typedef struct ol_arena_stats {
    size_t total_size;         /**< Total arena size in bytes */
    size_t used_size;          /**< Currently allocated bytes */
    size_t allocated_blocks;   /**< Number of active allocations */
    size_t free_blocks;        /**< Number of free blocks */
    size_t peak_usage;         /**< Peak memory usage reached */
    size_t cache_hits;         /**< Number of cache hits */
    size_t cache_misses;       /**< Number of cache misses */
    size_t fragmentation;      /**< Fragmentation percentage (0-100) */
} ol_arena_stats_t;

/**
 * @brief Arena allocation flags
 */
typedef enum {
    OL_ARENA_ZERO_COPY      = 1 << 0, /**< Enable zero-copy between arenas */
    OL_ARENA_SHARED         = 1 << 1, /**< Arena can be shared between processes */
    OL_ARENA_LOCK_FREE      = 1 << 2, /**< Use lock-free allocation */
    OL_ARENA_GUARD_PAGES    = 1 << 3, /**< Add guard pages for overflow detection */
    OL_ARENA_COMPACT        = 1 << 4, /**< Compact memory on free */
    OL_ARENA_PREFETCH       = 1 << 5, /**< Use prefetching for allocations */
} ol_arena_flags_t;

/**
 * @brief Memory arena for isolated actor allocations
 * 
 * @details Each actor gets its own arena with:
 * - Bump allocation for fast path
 * - Free list for reused allocations
 * - Size-class optimization for common allocations
 * - Cache alignment to prevent false sharing
 * - Guard pages for security
 */
typedef struct ol_arena ol_arena_t;

/* ==================== Arena API ==================== */

/**
 * @brief Create a new memory arena
 * @param size Initial size in bytes (0 = use default)
 * @param flags Arena configuration flags
 * @return New arena instance, NULL on failure
 * 
 * @note Default size is 1MB. Arena will expand automatically if needed.
 *       Memory is allocated with proper alignment for SIMD operations.
 */
ol_arena_t* ol_arena_create(size_t size, uint32_t flags) OL_MALLOC_LIKE;

/**
 * @brief Destroy arena and free all resources
 * @param arena Arena to destroy (can be NULL)
 * 
 * @note Safe to call with NULL. Also unmaps guard pages.
 */
void ol_arena_destroy(ol_arena_t* arena);

/**
 * @brief Allocate memory from arena with zero-copy optimization
 * @param arena Arena to allocate from
 * @param size Size to allocate in bytes
 * @return Allocated memory, NULL on failure
 * 
 * @note Returns 64-byte aligned memory for cache optimization.
 *       Uses bump allocation for fast path, free list for reuse.
 */
void* ol_arena_alloc(ol_arena_t* arena, size_t size) OL_MALLOC_LIKE OL_WARN_UNUSED_RESULT;

/**
 * @brief Allocate aligned memory from arena
 * @param arena Arena to allocate from
 * @param alignment Alignment requirement (must be power of 2)
 * @param size Size to allocate in bytes
 * @return Aligned memory, NULL on failure
 * 
 * @note Supports alignment up to 4KB pages.
 */
void* ol_arena_alloc_aligned(ol_arena_t* arena, size_t alignment, size_t size) 
    OL_MALLOC_LIKE OL_WARN_UNUSED_RESULT;

/**
 * @brief Free memory allocated from arena
 * @param arena Arena that memory belongs to
 * @param ptr Pointer to free
 * 
 * @note Memory is added to free list for reuse, not immediately freed.
 *       Performs validation to prevent double-free and corruption.
 */
void ol_arena_free(ol_arena_t* arena, void* ptr);

/**
 * @brief Reset arena (free all allocations quickly)
 * @param arena Arena to reset
 * 
 * @note Much faster than individual frees. Does not return memory to OS.
 *       Maintains arena size for future allocations.
 */
void ol_arena_reset(ol_arena_t* arena);

/**
 * @brief Get arena statistics
 * @param arena Arena to query
 * @param stats Output structure for statistics
 * @return 0 on success, -1 on error
 */
int ol_arena_get_stats(const ol_arena_t* arena, ol_arena_stats_t* stats);

/**
 * @brief Check if pointer belongs to arena
 * @param arena Arena to check
 * @param ptr Pointer to verify
 * @return true if pointer is within arena, false otherwise
 */
bool ol_arena_contains(const ol_arena_t* arena, const void* ptr) OL_PURE;

/**
 * @brief Get total arena size
 * @param arena Arena to query
 * @return Total size in bytes, 0 if arena is NULL
 */
size_t ol_arena_total_size(const ol_arena_t* arena) OL_PURE;

/**
 * @brief Get currently used arena size
 * @param arena Arena to query
 * @return Used size in bytes, 0 if arena is NULL
 */
size_t ol_arena_used_size(const ol_arena_t* arena) OL_PURE;

/**
 * @brief Expand arena if possible
 * @param arena Arena to expand
 * @param additional_size Additional size needed in bytes
 * @return 0 on success, -1 on failure
 * 
 * @note Uses mremap on Linux for zero-copy expansion.
 *       Falls back to copy on other platforms.
 */
int ol_arena_expand(ol_arena_t* arena, size_t additional_size);

/**
 * @brief Create zero-copy shared arena between processes
 * @param size Arena size in bytes
 * @param name Shared memory name (platform-specific)
 * @return Shared arena, NULL on failure
 * 
 * @note On Linux/BSD: uses shm_open + mmap
 *       On Windows: uses CreateFileMapping + MapViewOfFile
 *       On macOS: uses POSIX shared memory
 */
ol_arena_t* ol_arena_create_shared(size_t size, const char* name) OL_MALLOC_LIKE;

/**
 * @brief Attach to existing shared arena
 * @param name Shared memory name
 * @return Attached arena, NULL on failure
 */
ol_arena_t* ol_arena_attach_shared(const char* name) OL_MALLOC_LIKE;

/**
 * @brief Defragment arena to reduce memory fragmentation
 * @param arena Arena to defragment
 * @return 0 on success, -1 on failure
 * 
 * @note Moves allocated blocks together to create larger free blocks.
 *       Updates all pointers automatically.
 */
int ol_arena_defragment(ol_arena_t* arena);

/* ==================== Architecture-Specific Memory Operations ==================== */

/**
 * @brief Non-temporal memory copy (bypass cache)
 * @param dest Destination buffer (must be 64-byte aligned)
 * @param src Source buffer (must be 64-byte aligned)
 * @param n Number of bytes (multiple of 64)
 * 
 * @note Uses MOVNTDQ on x86, STNP on ARM for large copies
 */
void ol_memcpy_nt(void* OL_RESTRICT dest, const void* OL_RESTRICT src, size_t n) OL_HOT;

/**
 * @brief Non-temporal memory set (bypass cache)
 * @param dest Destination buffer (must be 64-byte aligned)
 * @param c Value to set
 * @param n Number of bytes (multiple of 64)
 */
void ol_memset_nt(void* dest, int c, size_t n) OL_HOT;

/**
 * @brief Prefetch multiple cache lines ahead
 * @param ptr Starting address
 * @param stride Number of bytes between prefetches
 * @param count Number of prefetches to issue
 */
static inline void ol_prefetch_range(const void* ptr, size_t stride, size_t count) OL_HOT;

/**
 * @brief Memory barrier for specific address ranges
 * @param ptr Starting address
 * @param size Range size
 */
static inline void ol_memory_barrier_range(void* ptr, size_t size);

/**
 * @brief Flush cache line from all CPU caches
 * @param ptr Pointer within cache line
 */
static inline void ol_clflush(const void* ptr) OL_HOT;

/**
 * @brief Flush and invalidate cache line
 * @param ptr Pointer within cache line
 */
static inline void ol_clflushopt(const void* ptr) OL_HOT;

/* ==================== Zero-Copy Buffer Management ==================== */

/**
 * @brief Zero-copy buffer for inter-process communication
 */
typedef struct ol_zero_copy_buffer {
    void* data;                     /**< Buffer data */
    size_t size;                    /**< Buffer size */
    uint32_t ref_count;             /**< Reference count for sharing */
    uint32_t flags;                 /**< Buffer flags */
    ol_arena_t* arena;              /**< Source arena */
    uint64_t magic;                 /**< Magic number for validation */
} ol_zero_copy_buffer_t;

/**
 * @brief Create zero-copy buffer from existing memory
 * @param arena Source arena
 * @param data Existing data pointer
 * @param size Data size
 * @return Zero-copy buffer, NULL on failure
 */
ol_zero_copy_buffer_t* ol_zero_copy_create(ol_arena_t* arena, void* data, size_t size) OL_MALLOC_LIKE;

/**
 * @brief Reference zero-copy buffer (increment ref count)
 * @param buffer Buffer to reference
 * @return New reference to buffer
 */
ol_zero_copy_buffer_t* ol_zero_copy_ref(ol_zero_copy_buffer_t* buffer);

/**
 * @brief Release zero-copy buffer (decrement ref count)
 * @param buffer Buffer to release
 * 
 * @note Frees buffer when ref count reaches zero
 */
void ol_zero_copy_unref(ol_zero_copy_buffer_t* buffer);

/**
 * @brief Transfer zero-copy buffer between arenas without copying
 * @param src_arena Source arena
 * @param dest_arena Destination arena
 * @param buffer Buffer to transfer
 * @return Transferred buffer in destination arena, NULL on failure
 */
ol_zero_copy_buffer_t* ol_zero_copy_transfer(ol_arena_t* src_arena, 
                                            ol_arena_t* dest_arena,
                                            ol_zero_copy_buffer_t* buffer);

/* ==================== Guard Page Management ==================== */

/**
 * @brief Add guard pages around memory region
 * @param ptr Memory region start
 * @param size Region size
 * @return 0 on success, -1 on failure
 */
int ol_guard_pages_add(void* ptr, size_t size);

/**
 * @brief Remove guard pages
 * @param ptr Memory region start
 * @param size Region size
 */
void ol_guard_pages_remove(void* ptr, size_t size);

/**
 * @brief Check if guard page violation occurred
 * @param ptr Suspected address
 * @return true if violation detected, false otherwise
 */
bool ol_guard_page_violation(const void* ptr);

/* ==================== Cache-Optimized Structures ==================== */

/**
 * @brief Cache-aligned atomic counter to prevent false sharing
 */
typedef struct ol_cache_aligned_counter {
    alignas(OL_CACHE_LINE_SIZE) volatile uint64_t value;
    char padding[OL_CACHE_LINE_SIZE - sizeof(uint64_t)];
} ol_cache_aligned_counter_t;

/**
 * @brief Cache-aligned ring buffer for lock-free messaging
 */
typedef struct ol_cache_aligned_ring {
    alignas(OL_CACHE_LINE_SIZE) volatile size_t head;
    alignas(OL_CACHE_LINE_SIZE) volatile size_t tail;
    alignas(OL_CACHE_LINE_SIZE) void* buffer[];
} ol_cache_aligned_ring_t;

/* ==================== Inline Function Implementations ==================== */

#if OL_ARCH_X86_64 && (defined(__GNUC__) || defined(__clang__))

static inline void ol_clflush(const void* ptr) {
    _mm_clflush(ptr);
}

static inline void ol_clflushopt(const void* ptr) {
    #if defined(__AVX512F__)
        _mm_clflushopt(ptr);
    #else
        _mm_clflush(ptr);
    #endif
}

static inline void ol_prefetch_range(const void* ptr, size_t stride, size_t count) {
    const char* p = (const char*)ptr;
    for (size_t i = 0; i < count; i++) {
        __builtin_prefetch(p + i * stride, 0, 3);
    }
}

static inline void ol_memory_barrier_range(void* ptr, size_t size) {
    // Use CLFLUSHOPT for range flush on x86
    char* p = (char*)ptr;
    char* end = p + size;
    for (; p < end; p += OL_CACHE_LINE_SIZE) {
        ol_clflushopt(p);
    }
    OL_MEMORY_BARRIER();
}

#elif OL_ARCH_ARM64 && (defined(__GNUC__) || defined(__clang__))

static inline void ol_clflush(const void* ptr) {
    __asm__ volatile("dc civac, %0" : : "r"(ptr) : "memory");
}

static inline void ol_clflushopt(const void* ptr) {
    // ARM doesn't have CLFLUSHOPT equivalent, use full clean
    __asm__ volatile("dc civac, %0" : : "r"(ptr) : "memory");
}

static inline void ol_prefetch_range(const void* ptr, size_t stride, size_t count) {
    const char* p = (const char*)ptr;
    for (size_t i = 0; i < count; i++) {
        __asm__ volatile("prfm pldl1keep, [%0]" : : "r"(p + i * stride) :);
    }
}

static inline void ol_memory_barrier_range(void* ptr, size_t size) {
    // ARM requires explicit cache maintenance
    char* p = (char*)ptr;
    char* end = p + size;
    for (; p < end; p += OL_CACHE_LINE_SIZE) {
        __asm__ volatile("dc civac, %0" : : "r"(p) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");
}

#else
// Fallback implementations for other architectures

static inline void ol_clflush(const void* ptr) {
    OL_COMPILER_BARRIER();
}

static inline void ol_clflushopt(const void* ptr) {
    OL_COMPILER_BARRIER();
}

static inline void ol_prefetch_range(const void* ptr, size_t stride, size_t count) {
    const char* p = (const char*)ptr;
    for (size_t i = 0; i < count; i++) {
        OL_PREFETCH_READ(p + i * stride);
    }
}

static inline void ol_memory_barrier_range(void* ptr, size_t size) {
    OL_MEMORY_BARRIER();
}

#endif

/* ==================== Compiler Attributes ==================== */

#if defined(__GNUC__) || defined(__clang__)
    #define OL_MALLOC_LIKE __attribute__((malloc))
    #define OL_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
    #define OL_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
    #define OL_MALLOC_LIKE __declspec(restrict)
    #define OL_WARN_UNUSED_RESULT _Check_return_
    #define OL_NORETURN __declspec(noreturn)
#else
    #define OL_MALLOC_LIKE
    #define OL_WARN_UNUSED_RESULT
    #define OL_NORETURN
#endif

#endif /* OL_ACTOR_ISOLATION_H */