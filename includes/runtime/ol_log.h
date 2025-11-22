#ifndef OL_LOG_H
#define OL_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

/* -----------------------------------------------------
 * Log levels
 * These are critical because they define severity
 * and control filtering of log output.
 * ----------------------------------------------------- */
typedef enum {
    OL_LOG_DEBUG = 0,  // Detailed debug information
    OL_LOG_INFO,       // General runtime information
    OL_LOG_WARN,       // Warnings about potential issues
    OL_LOG_ERROR       // Errors that require attention
} ol_log_level_t;

/* -----------------------------------------------------
 * Global logging configuration
 * This structure allows runtime to control logging
 * behavior dynamically (e.g., enabling/disabling debug).
 * ----------------------------------------------------- */
typedef struct {
    ol_log_level_t current_level;   // Minimum level to output
    bool           to_stdout;       // Output logs to stdout
    bool           to_file;         // Output logs to file
    FILE          *file_handle;     // File handle if logging to file
} ol_log_config_t;

/* -----------------------------------------------------
 * Global log configuration instance
 * Defined in ol_log.c
 * ----------------------------------------------------- */
extern ol_log_config_t OL_LOG_CONFIG;

/* -----------------------------------------------------
 * Logging functions
 * These functions are complex because they handle
 * variable arguments and formatting.
 * ----------------------------------------------------- */

/**
 * @brief Initialize logging system.
 * @param level Minimum log level to output.
 * @param log_to_file Enable/disable file logging.
 * @param filename Path to log file (if log_to_file is true).
 * @return 0 on success, negative on failure.
 */
int ol_log_init(ol_log_level_t level, bool log_to_file, const char *filename);

/**
 * @brief Shutdown logging system and release resources.
 */
void ol_log_shutdown(void);

/**
 * @brief Write a log message with given severity.
 * @param level Log severity level.
 * @param fmt Format string (printf-style).
 * @param ... Variable arguments.
 */
void ol_log_write(ol_log_level_t level, const char *fmt, ...);

/* -----------------------------------------------------
 * Convenience macros
 * These macros simplify usage and automatically
 * insert file/line information for debugging.
 * ----------------------------------------------------- */
#define OL_LOGD(fmt, ...) ol_log_write(OL_LOG_DEBUG, "[DEBUG] %s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define OL_LOGI(fmt, ...) ol_log_write(OL_LOG_INFO,  "[INFO]  %s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define OL_LOGW(fmt, ...) ol_log_write(OL_LOG_WARN,  "[WARN]  %s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define OL_LOGE(fmt, ...) ol_log_write(OL_LOG_ERROR, "[ERROR] %s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#endif /* OL_LOG_H */
