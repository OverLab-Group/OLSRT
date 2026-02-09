#include "ol_globals.h"
#include <stdio.h>
#include <string.h>

/* -----------------------------------------------------
 * Global variable definition
 * ----------------------------------------------------- */
ol_runtime_globals_t OL_GLOBALS;

/* -----------------------------------------------------
 * Initialize global runtime variables
 * This function sets default values for the runtime
 * environment. It is critical because other modules
 * depend on these globals being correctly initialized.
 * ----------------------------------------------------- */
int ol_globals_init(void) {
    OL_GLOBALS.thread_count = 0;       // No threads active at startup
    OL_GLOBALS.stream_count = 0;       // No streams active at startup
    OL_GLOBALS.debug_mode   = false;   // Debug disabled by default
    strncpy(OL_GLOBALS.runtime_id, "OLSRT_RUNTIME_DEFAULT", sizeof(OL_GLOBALS.runtime_id) - 1);

    // Ensure null termination of runtime_id
    OL_GLOBALS.runtime_id[sizeof(OL_GLOBALS.runtime_id) - 1] = '\0';

    return OL_STATUS_OK;
}

/* -----------------------------------------------------
 * Shutdown global runtime variables
 * This function resets global state and performs cleanup.
 * Important for preventing resource leaks when runtime
 * is stopped or restarted.
 * ----------------------------------------------------- */
int ol_globals_shutdown(void) {
    OL_GLOBALS.thread_count = 0;
    OL_GLOBALS.stream_count = 0;
    OL_GLOBALS.debug_mode   = false;
    memset(OL_GLOBALS.runtime_id, 0, sizeof(OL_GLOBALS.runtime_id));

    return OL_STATUS_OK;
}

/* -----------------------------------------------------
 * Get runtime version string
 * Converts version macros into a human-readable string.
 * Useful for CLI output and logging.
 * ----------------------------------------------------- */
void ol_get_version(char *buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return; // Defensive check
    }

    snprintf(buffer, size,
             "OLSRT v%d.%d.%d",
             OL_VERSION_MAJOR,
             OL_VERSION_MINOR,
             OL_VERSION_PATCH);
}
