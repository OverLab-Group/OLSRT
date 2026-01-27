/**
 * @file ol_platform.h
 * @brief Platform detection and abstraction macros
 */

#ifndef OL_PLATFORM_H
#define OL_PLATFORM_H

/* Architecture detection */
#if defined(__x86_64__) || defined(_M_X64)
#define OL_ARCH_X86_64 1
#define OL_ARCH_64BIT 1
#elif defined(__i386__) || defined(_M_IX86)
#define OL_ARCH_X86 1
#define OL_ARCH_32BIT 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define OL_ARCH_ARM64 1
#define OL_ARCH_64BIT 1
#elif defined(__arm__) || defined(_M_ARM)
#define OL_ARCH_ARM 1
#define OL_ARCH_32BIT 1
#elif defined(__riscv) && (__riscv_xlen == 64)
#define OL_ARCH_RISCV64 1
#define OL_ARCH_64BIT 1
#elif defined(__powerpc64__) || defined(__ppc64__)
#define OL_ARCH_PPC64 1
#define OL_ARCH_64BIT 1
#else
#define OL_ARCH_UNKNOWN 1
#endif

/* OS detection */
#if defined(_WIN32) || defined(_WIN64)
#define OL_OS_WINDOWS 1
#define OL_OS_NAME "Windows"
#elif defined(__APPLE__) && defined(__MACH__)
#define OL_OS_MACOS 1
#define OL_OS_NAME "macOS"
#include <TargetConditionals.h>
#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
#define OL_OS_IOS 1
#endif
#elif defined(__linux__)
#define OL_OS_LINUX 1
#define OL_OS_NAME "Linux"
#elif defined(__FreeBSD__)
#define OL_OS_FREEBSD 1
#define OL_OS_NAME "FreeBSD"
#elif defined(__OpenBSD__)
#define OL_OS_OPENBSD 1
#define OL_OS_NAME "OpenBSD"
#elif defined(__NetBSD__)
#define OL_OS_NETBSD 1
#define OL_OS_NAME "NetBSD"
#elif defined(__DragonFly__)
#define OL_OS_DRAGONFLY 1
#define OL_OS_NAME "DragonFly"
#else
#define OL_OS_UNKNOWN 1
#define OL_OS_NAME "Unknown"
#endif

/* Compiler detection */
#if defined(__clang__)
#define OL_COMPILER_CLANG 1
#define OL_COMPILER_VERSION __clang_major__
#elif defined(__GNUC__)
#define OL_COMPILER_GCC 1
#define OL_COMPILER_VERSION __GNUC__
#elif defined(_MSC_VER)
#define OL_COMPILER_MSVC 1
#define OL_COMPILER_VERSION _MSC_VER
#else
#define OL_COMPILER_UNKNOWN 1
#endif

/* Feature detection */
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
#define OL_HAS_POSIX 1
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define OL_HAS_C11 1
#endif

#if defined(__cplusplus) && __cplusplus >= 201103L
#define OL_HAS_CPP11 1
#endif

/* Alignment macros */
#define OL_ALIGN(x) __attribute__((aligned(x)))
#define OL_CACHELINE_SIZE 64
#define OL_CACHELINE_ALIGN OL_ALIGN(OL_CACHELINE_SIZE)

/* Branch prediction */
#if defined(__GNUC__) || defined(__clang__)
#define OL_LIKELY(x)   __builtin_expect(!!(x), 1)
#define OL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define OL_LIKELY(x)   (x)
#define OL_UNLIKELY(x) (x)
#endif

/* No-return function */
#if defined(__GNUC__) || defined(__clang__)
#define OL_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define OL_NORETURN __declspec(noreturn)
#else
#define OL_NORETURN
#endif

/* Packed structures */
#if defined(__GNUC__) || defined(__clang__)
#define OL_PACKED __attribute__((packed))
#elif defined(_MSC_VER)
#define OL_PACKED
#pragma pack(push, 1)
#else
#define OL_PACKED
#endif

/* Inline functions */
#if defined(_MSC_VER)
#define OL_INLINE __inline
#define OL_FORCEINLINE __forceinline
#else
#define OL_INLINE inline
#define OL_FORCEINLINE inline __attribute__((always_inline))
#endif

/* Thread-local storage */
#if defined(_MSC_VER)
#define OL_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define OL_THREAD_LOCAL __thread
#else
#define OL_THREAD_LOCAL
#endif

/* Atomic operations abstraction */
#include <stdatomic.h>

/* Platform-specific includes */
#if OL_OS_WINDOWS
#include <windows.h>
#include <intrin.h>
#else
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#endif

/* Yield processor */
static OL_INLINE void ol_cpu_yield(void) {
    #if OL_OS_WINDOWS
    YieldProcessor();
    #elif defined(__x86_64__) || defined(__i386__)
    asm volatile("pause" ::: "memory");
    #elif defined(__aarch64__)
    asm volatile("yield" ::: "memory");
    #else
    sched_yield();
    #endif
}

/* Memory barrier */
static OL_INLINE void ol_memory_barrier(void) {
    #if defined(__GNUC__) || defined(__clang__)
    asm volatile("" ::: "memory");
    #elif OL_OS_WINDOWS
    MemoryBarrier();
    #else
    __sync_synchronize();
    #endif
}

/* Get thread ID */
static OL_INLINE uint64_t ol_get_thread_id(void) {
    #if OL_OS_WINDOWS
    return GetCurrentThreadId();
    #elif OL_OS_LINUX
    return syscall(SYS_gettid);
    #elif OL_OS_MACOS
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid;
    #else
    return (uint64_t)pthread_self();
    #endif
}

/* Get page size */
static OL_INLINE size_t ol_get_page_size(void) {
    #if OL_OS_WINDOWS
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    return sys_info.dwPageSize;
    #else
    return sysconf(_SC_PAGESIZE);
    #endif
}

/* Get number of CPUs */
static OL_INLINE int ol_get_cpu_count(void) {
    #if OL_OS_WINDOWS
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    return sys_info.dwNumberOfProcessors;
    #else
    return sysconf(_SC_NPROCESSORS_ONLN);
    #endif
}

#endif /* OL_PLATFORM_H */
