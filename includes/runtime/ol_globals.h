#ifndef OL_GLOBALS_H
#define OL_GLOBALS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* -----------------------------------------------------
 * Runtime version information
 * ----------------------------------------------------- */
#define OL_VERSION_MAJOR   1
#define OL_VERSION_MINOR   0
#define OL_VERSION_PATCH   0

/* -----------------------------------------------------
 * Core runtime configuration
 * ----------------------------------------------------- */
#define OL_MAX_THREADS     64      // Maximum number of worker threads
#define OL_MAX_STREAMS     1024    // Maximum concurrent streams
#define OL_LOG_BUFFER_SIZE 8192    // Log buffer size (critical for performance)

/* -----------------------------------------------------
 * Global status codes
 * ----------------------------------------------------- */
typedef enum {
    OL_STATUS_OK = 0,       // Operation successful
    OL_STATUS_ERROR = -1,   // Generic error
    OL_STATUS_TIMEOUT = -2, // Timeout occurred
    OL_STATUS_BUSY = -3     // Resource busy
} ol_status_t;

/* -----------------------------------------------------
 * Runtime global state
 * This structure holds the most important runtime-wide
 * variables. It is shared across all modules.
 * ----------------------------------------------------- */
typedef struct {
    uint32_t thread_count;     // Active threads in runtime
    uint32_t stream_count;     // Active streams in runtime
    bool     debug_mode;       // Debug mode flag
    char     runtime_id[64];   // Unique runtime identifier
} ol_runtime_globals_t;

/* -----------------------------------------------------
 * Global variable declaration
 * Defined in ol_globals.c
 * ----------------------------------------------------- */
extern ol_runtime_globals_t OL_GLOBALS;

/* -----------------------------------------------------
 * Global functions
 * These functions are critical for initializing and
 * shutting down the runtime environment.
 * ----------------------------------------------------- */
int ol_globals_init(void);       // Initialize global runtime variables
int ol_globals_shutdown(void);   // Cleanup global runtime variables
void ol_get_version(char *buffer, size_t size); // Return runtime version string

#endif /* OL_GLOBALS_H */
