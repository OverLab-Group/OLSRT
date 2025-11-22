#include "ol_status.h"

/* -----------------------------------------------------
 * Convert status code to string
 * This function is important because it provides
 * human-readable output for CLI and logging.
 * ----------------------------------------------------- */
const char* ol_status_to_string(ol_status_t status) {
    switch (status) {
        case OL_STATUS_SUCCESS:
            return "Success";
        case OL_STATUS_FAILURE:
            return "Failure";
        case OL_STATUS_TIMEOUT:
            return "Timeout";
        case OL_STATUS_INVALID:
            return "Invalid argument or state";
        case OL_STATUS_NOT_FOUND:
            return "Resource not found";
        case OL_STATUS_BUSY:
            return "Resource busy";
        case OL_STATUS_UNSUPPORTED:
            return "Feature not supported";
        default:
            return "Unknown status";
    }
}
