#ifndef OL_SUPERVISOR_H
#define OL_SUPERVISOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_lock_mutex.h"
#include "ol_channel.h"
#include "ol_deadlines.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ol_actor_fn)(void *arg);

/* Supervisor strategy */
typedef enum {
    OL_SUP_ONE_FOR_ONE = 0,
    OL_SUP_ONE_FOR_ALL,
    OL_SUP_REST_FOR_ONE
} ol_supervisor_strategy_t;

/* Child restart policy */
typedef enum {
    OL_CHILD_PERMANENT = 0, /* always restart when exits (even normal) */
    OL_CHILD_TRANSIENT,     /* restart only on failure (non-zero exit) */
    OL_CHILD_TEMPORARY      /* never restart; removed after exit */
} ol_restart_policy_t;

/* Child specification (immutable once added) */
typedef struct {
    const char *name;            /* optional label (not owned) */
    ol_actor_fn fn;              /* entry function: int (*fn)(void*) */
    void       *arg;             /* user data passed to fn */
    ol_restart_policy_t policy;  /* restart decision */
    int64_t     shutdown_timeout_ns; /* graceful stop timeout (0 => immediate) */
} ol_child_spec_t;

/* Opaque supervisor type */
typedef struct ol_supervisor ol_supervisor_t;

/* Create a supervisor with given strategy and intensity window.
 * max_restarts: maximum restarts allowed within 'window_ms' before escalation.
 * window_ms: time window in milliseconds to measure restart intensity.
 */
ol_supervisor_t* ol_supervisor_create(ol_supervisor_strategy_t strategy,
                                      int max_restarts,
                                      int window_ms);

/* Destroy supervisor object; shuts down any remaining children. */
void ol_supervisor_destroy(ol_supervisor_t *sup);

/* Start supervisor monitoring loop (spawns internal monitor thread).
 * Returns 0 on success.
 */
int ol_supervisor_start(ol_supervisor_t *sup);

/* Stop supervisor: shuts down children according to their timeouts, stops monitor. */
int ol_supervisor_stop(ol_supervisor_t *sup);

/* Add a child and start it. Returns child id (positive) on success; 0 on failure. */
uint32_t ol_supervisor_add_child(ol_supervisor_t *sup, const ol_child_spec_t *spec);

/* Remove a child (if running, it's stopped first). Returns 0 on success. */
int ol_supervisor_remove_child(ol_supervisor_t *sup, uint32_t child_id);

/* Restart a child manually (even if running, it will be stopped and restarted). */
int ol_supervisor_restart_child(ol_supervisor_t *sup, uint32_t child_id);

/* Introspection */
size_t   ol_supervisor_child_count(const ol_supervisor_t *sup);
bool     ol_supervisor_is_running(const ol_supervisor_t *sup);

#ifdef __cplusplus
}
#endif

#endif /* OL_SUPERVISOR_H */
