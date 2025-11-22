#ifndef OL_EVENT_LOOP_H
#define OL_EVENT_LOOP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ol_deadlines.h"
#include "ol_poller.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Event type dispatched by the loop */
typedef enum {
    OL_EV_NONE = 0,
    OL_EV_IO,        /* readiness on a file descriptor */
    OL_EV_TIMER      /* software timer fired */
} ol_ev_type_t;

/* Forward declaration for callback signature */
struct ol_event_loop;

/* Callback invoked when an event fires.
 * - loop: event loop instance
 * - type: IO or TIMER
 * - fd:   valid for IO events, -1 for TIMER
 * - user_data: user-provided context from registration
 * Return value is ignored; callback must not longjmp/exit.
 */
typedef void (*ol_event_cb)(
    struct ol_event_loop *loop,
    ol_ev_type_t type,
    int fd,
    void *user_data
);

/* Opaque loop structure */
typedef struct ol_event_loop ol_event_loop_t;

/* Lifecycle */
ol_event_loop_t* ol_event_loop_create(void);
void             ol_event_loop_destroy(ol_event_loop_t *loop);

/* Control */
int  ol_event_loop_run(ol_event_loop_t *loop);      /* blocks until stop is called */
void ol_event_loop_stop(ol_event_loop_t *loop);     /* request graceful stop */
int  ol_event_loop_wake(ol_event_loop_t *loop);     /* wake the loop (non-blocking) */

/* Registration: I/O (caller owns fd; loop never closes it) 
 * mask: OR of OL_POLL_IN | OL_POLL_OUT | OL_POLL_ERR
 * Returns non-zero event id on success, 0 on failure.
 */
uint64_t ol_event_loop_register_io(ol_event_loop_t *loop, int fd, uint32_t mask, ol_event_cb cb, void *user_data);

/* Modify interest mask/tag for an existing I/O event. Returns 0 on success. */
int      ol_event_loop_mod_io(ol_event_loop_t *loop, uint64_t id, uint32_t mask);

/* Registration: one-shot timer that fires at given absolute deadline.
 * If periodic_ns > 0, the timer is periodic with that interval.
 * Returns non-zero event id on success, 0 on failure.
 */
uint64_t ol_event_loop_register_timer(ol_event_loop_t *loop, ol_deadline_t deadline, int64_t periodic_ns, ol_event_cb cb, void *user_data);

/* Deregistration for both IO and TIMER. Returns 0 on success, negative on failure. */
int      ol_event_loop_unregister(ol_event_loop_t *loop, uint64_t id);

/* Introspection (optional helpers) */
bool     ol_event_loop_is_running(const ol_event_loop_t *loop);
size_t   ol_event_loop_event_count(const ol_event_loop_t *loop);

#ifdef __cplusplus
}
#endif

#endif /* OL_EVENT_LOOP_H */
