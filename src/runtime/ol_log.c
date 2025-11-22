#include "ol_log.h"
#include <string.h>
#include <time.h>

/* -----------------------------------------------------
 * Global log configuration instance
 * ----------------------------------------------------- */
ol_log_config_t OL_LOG_CONFIG;

/* -----------------------------------------------------
 * Internal helper: get timestamp string
 * This function is important because it ensures
 * all log entries are timestamped consistently.
 * ----------------------------------------------------- */
static void ol_log_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/* -----------------------------------------------------
 * Initialize logging system
 * Opens file if requested and sets configuration.
 * ----------------------------------------------------- */
int ol_log_init(ol_log_level_t level, bool log_to_file, const char *filename) {
    OL_LOG_CONFIG.current_level = level;
    OL_LOG_CONFIG.to_stdout     = true;   // Always allow stdout
    OL_LOG_CONFIG.to_file       = log_to_file;
    OL_LOG_CONFIG.file_handle   = NULL;

    if (log_to_file && filename != NULL) {
        OL_LOG_CONFIG.file_handle = fopen(filename, "a");
        if (OL_LOG_CONFIG.file_handle == NULL) {
            return -1; // Failed to open log file
        }
    }

    return 0;
}

/* -----------------------------------------------------
 * Shutdown logging system
 * Closes file handle if used.
 * ----------------------------------------------------- */
void ol_log_shutdown(void) {
    if (OL_LOG_CONFIG.to_file && OL_LOG_CONFIG.file_handle != NULL) {
        fclose(OL_LOG_CONFIG.file_handle);
        OL_LOG_CONFIG.file_handle = NULL;
    }
}

/* -----------------------------------------------------
 * Write a log message
 * Handles severity filtering, timestamping, and
 * variable argument formatting.
 * ----------------------------------------------------- */
void ol_log_write(ol_log_level_t level, const char *fmt, ...) {
    if (level < OL_LOG_CONFIG.current_level) {
        return; // Skip messages below current level
    }

    char timestamp[32];
    ol_log_timestamp(timestamp, sizeof(timestamp));

    // Prepare formatted message
    va_list args;
    va_start(args, fmt);

    char message[1024];
    vsnprintf(message, sizeof(message), fmt, args);

    va_end(args);

    // Output to stdout
    if (OL_LOG_CONFIG.to_stdout) {
        fprintf(stdout, "[%s] %s\n", timestamp, message);
    }

    // Output to file if enabled
    if (OL_LOG_CONFIG.to_file && OL_LOG_CONFIG.file_handle != NULL) {
        fprintf(OL_LOG_CONFIG.file_handle, "[%s] %s\n", timestamp, message);
        fflush(OL_LOG_CONFIG.file_handle); // Ensure immediate write
    }
}
