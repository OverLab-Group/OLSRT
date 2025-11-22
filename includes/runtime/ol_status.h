#ifndef OL_STATUS_H
#define OL_STATUS_H

#include <stdint.h>

/* -----------------------------------------------------
 * Status codes
 * These codes are critical for error handling across
 * the runtime. They provide a unified way to represent
 * success, failure, and special conditions.
 * ----------------------------------------------------- */
typedef enum {
    OL_STATUS_SUCCESS = 0,   // Operation completed successfully
    OL_STATUS_FAILURE = -1,  // Generic failure
    OL_STATUS_TIMEOUT = -2,  // Operation timed out
    OL_STATUS_INVALID = -3,  // Invalid argument or state
    OL_STATUS_NOT_FOUND = -4,// Resource not found
    OL_STATUS_BUSY = -5,     // Resource currently busy
    OL_STATUS_UNSUPPORTED = -6 // Feature not supported
} ol_status_t;

/* -----------------------------------------------------
 * Helper macros
 * These macros simplify checking status codes and
 * improve readability in runtime modules.
 * ----------------------------------------------------- */
#define OL_IS_OK(status)        ((status) == OL_STATUS_SUCCESS)
#define OL_IS_ERROR(status)     ((status) < 0)
#define OL_IS_TIMEOUT(status)   ((status) == OL_STATUS_TIMEOUT)
#define OL_IS_INVALID(status)   ((status) == OL_STATUS_INVALID)

/* -----------------------------------------------------
 * Function prototypes
 * These functions provide human-readable descriptions
 * of status codes, useful for CLI and logging.
 * ----------------------------------------------------- */

/**
 * @brief Convert status code to string.
 * @param status Status code.
 * @return Constant string describing the status.
 */
const char* ol_status_to_string(ol_status_t status);

#endif /* OL_STATUS_H */
