/**
 * @file ol_memwatch.h
 * @brief Memory leak detection and monitoring utilities
 */

#ifndef OL_MEMWATCH_H
#define OL_MEMWATCH_H

#include <stddef.h>
#include <stdbool.h>

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

    #ifdef __cplusplus
}
#endif

#endif /* OL_MEMWATCH_H */
