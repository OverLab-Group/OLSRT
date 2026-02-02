/**
 * @file ol_memwatch.h
 * @brief Memory leak detection and monitoring utilities - Cross Platform
 */

#ifndef OL_MEMWATCH_H
#define OL_MEMWATCH_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize memory watcher
 * @return 0 on success, -1 on failure
 */
int ol_memwatch_init(void);

/**
 * @brief Shutdown memory watcher and report leaks
 */
void ol_memwatch_shutdown(void);

/**
 * @brief Enable/disable memory tracking
 * @param enabled true to enable, false to disable
 */
void ol_memwatch_enable(bool enabled);

/**
 * @brief Get current memory usage in bytes
 * @return bytes allocated, 0 if not initialized
 */
size_t ol_memwatch_get_usage(void);

/**
 * @brief Get peak memory usage in bytes
 * @return peak bytes allocated
 */
size_t ol_memwatch_get_peak(void);

/**
 * @brief Dump current memory allocation state to stderr
 */
void ol_memwatch_dump(void);

/**
 * @brief Set allocation threshold for leak reporting
 * @param threshold minimum bytes to report
 */
void ol_memwatch_set_threshold(size_t threshold);

/**
 * @brief Track allocation with file and line information
 * @param size Allocation size
 * @param file Source file name
 * @param line Line number
 * @return Pointer to allocated memory
 */
void *ol_memwatch_track_alloc(size_t size, const char *file, int line);

/**
 * @brief Track memory free with file and line information
 * @param ptr Pointer to free
 * @param file Source file name
 * @param line Line number
 */
void ol_memwatch_track_free(void *ptr, const char *file, int line);

/* Wrapped allocation functions */
void *ol_memwatch_malloc(size_t size);
void *ol_memwatch_calloc(size_t nmemb, size_t size);
void *ol_memwatch_realloc(void *ptr, size_t size);
void ol_memwatch_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* OL_MEMWATCH_H */