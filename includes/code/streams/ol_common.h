/**
 * @file ol_common.h
 * @brief Common definitions, macros, and platform abstractions for the OLSRT async runtime
 * @version 1.2.0
 * @date 2024
 * 
 * This header provides cross-platform abstractions and common definitions used
 * throughout the OLSRT async runtime. It handles platform detection, compiler
 * hints, and basic type definitions.
 */

#ifndef OL_COMMON_H
#define OL_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/* Platform detection */
#if defined(_WIN32) || defined(_WIN64)
    #define OL_PLATFORM_WINDOWS 1
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#elif defined(__APPLE__) && defined(__MACH__)
    #define OL_PLATFORM_MACOS 1
    #define OL_PLATFORM_BSD 1
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC
        #define OL_PLATFORM_MACOSX 1
    #endif
#elif defined(__linux__)
    #define OL_PLATFORM_LINUX 1
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    #define OL_PLATFORM_BSD 1
#else
    #error "Unsupported platform"
#endif

/* Include standard headers FIRST before any macro definitions */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Compiler feature detection */
#if defined(__GNUC__) || defined(__clang__)
    #define OL_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define OL_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #define OL_NOINLINE    __attribute__((noinline))
    #define OL_ALWAYS_INLINE __attribute__((always_inline))
    #define OL_PRINTF_FORMAT(fmt_idx, arg_idx) \
        __attribute__((format(printf, fmt_idx, arg_idx)))
#elif defined(_MSC_VER)
    #define OL_LIKELY(x)   (x)
    #define OL_UNLIKELY(x) (x)
    #define OL_NOINLINE    __declspec(noinline)
    #define OL_ALWAYS_INLINE __forceinline
    #define OL_PRINTF_FORMAT(fmt_idx, arg_idx)
#else
    #define OL_LIKELY(x)   (x)
    #define OL_UNLIKELY(x) (x)
    #define OL_NOINLINE
    #define OL_ALWAYS_INLINE
    #define OL_PRINTF_FORMAT(fmt_idx, arg_idx)
#endif

/* Export/import macros for shared libraries */
#if defined(_WIN32)
    #ifdef OL_BUILDING_DLL
        #define OL_API __declspec(dllexport)
    #else
        #define OL_API __declspec(dllimport)
    #endif
    #define OL_LOCAL
#else
    #if __GNUC__ >= 4
        #define OL_API __attribute__((visibility("default")))
        #define OL_LOCAL __attribute__((visibility("hidden")))
    #else
        #define OL_API
        #define OL_LOCAL
    #endif
#endif

/* Function attribute macros */
#if defined(__GNUC__) || defined(__clang__)
    #define OL_MALLOC_LIKE __attribute__((malloc))
    #define OL_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
    #define OL_PURE __attribute__((pure))
    #define OL_HOT __attribute__((hot))
    #define OL_COLD __attribute__((cold))
    #define OL_RESTRICT __restrict__
#else
    #define OL_MALLOC_LIKE
    #define OL_WARN_UNUSED_RESULT
    #define OL_PURE
    #define OL_HOT
    #define OL_COLD
    #define OL_RESTRICT
#endif

/* Cache line size detection - Already defined above with standard headers */
#ifndef OL_CACHE_LINE_SIZE
    #if defined(__x86_64__) || defined(__i386__) || defined(__aarch64__)
        #define OL_CACHE_LINE_SIZE 64
    #elif defined(__arm__)
        #define OL_CACHE_LINE_SIZE 32
    #else
        #define OL_CACHE_LINE_SIZE 64
    #endif
#endif

/* Alignment macro */
#if defined(__GNUC__) || defined(__clang__)
    #define OL_ALIGNED(x) __attribute__((aligned(x)))
#elif defined(_MSC_VER)
    #define OL_ALIGNED(x) __declspec(align(x))
#else
    #define OL_ALIGNED(x)
#endif

/* Thread-local storage */
#if defined(_WIN32)
    #define OL_THREAD_LOCAL __declspec(thread)
#else
    #define OL_THREAD_LOCAL __thread
#endif

/* Atomic operations hint */
#if defined(__GNUC__) || defined(__clang__)
    #define OL_ATOMIC _Atomic
#else
    #define OL_ATOMIC
#endif

/* Error codes */
#define OL_SUCCESS       0
#define OL_ERROR        -1
#define OL_AGAIN        -2
#define OL_TIMEOUT      -3
#define OL_CLOSED       -4
#define OL_INVALID_ARG  -5
#define OL_NOMEM        -6

/**
 * @brief Result type for functions that can fail
 */
typedef struct {
    int code;      /**< Error code (0 for success, negative for errors) */
    const char *message; /**< Optional error message */
} ol_result_t;

/**
 * @brief Generic destructor function type
 * @param ptr Pointer to the object to destroy
 */
typedef void (*ol_destructor_fn)(void *ptr);

/**
 * @brief Generic callback function type
 * @param arg User-provided argument
 */
typedef void (*ol_callback_fn)(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* OL_COMMON_H */