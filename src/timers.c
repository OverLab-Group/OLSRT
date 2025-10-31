/* OLSRT - Timers */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>

/* Forward declaration from loop to access timer list head */
struct ol_loop_s;

/* Timer object (forward-declared in header) */
struct ol_timer_s {
    ol_loop_t   *loop;
    uint64_t     deadline_ms;  /* next fire time (monotonic) */
    uint64_t     period_ms;    /* 0 for one-shot, >0 for periodic */
    int          active;       /* 0 inactive, 1 active */
    ol_timer_cb  cb;
    void        *arg;
    struct ol_timer_s *next;   /* singly-linked list per loop */
};

/* Internal: linked list head accessor (stored on loop) */
static inline struct ol_timer_s** ol_loop_timers_head(ol_loop_t *l) {
    /* loop->timers is declared in the loop section */
    return (struct ol_timer_s**)&(((struct ol_loop_s*)l)->timers);
}

/* Create and start a timer: delay_ms until first fire; period_ms for repeat */
ol_timer_t* ol_timer_start(ol_loop_t *loop, uint64_t delay_ms, uint64_t period_ms,
                           ol_timer_cb cb, void *arg) {
    if (!loop || !cb) return NULL;
    ol_timer_t *t = (ol_timer_t*)calloc(1, sizeof(*t));
    if (!t) return NULL;

    t->loop        = loop;
    t->deadline_ms = ol_monotonic_ms() + delay_ms;
    t->period_ms   = period_ms;
    t->active      = 1;
    t->cb          = cb;
    t->arg         = arg;

    /* Insert at head for O(1) add; traversal order doesn't matter */
    t->next = *ol_loop_timers_head(loop);
    *ol_loop_timers_head(loop) = t;

    return t;
}

/* Stop a timer (idempotent). Does not remove from list; marks inactive. */
int ol_timer_stop(ol_timer_t *t) {
    if (!t) return OL_ERR_STATE;
    t->active = 0;
    return OL_OK;
}

/* Query active flag */
int ol_timer_is_active(ol_timer_t *t) {
    return t ? t->active : 0;
}

/* Internal: process timers on the loop (called from ol_loop_tick) */
static void ol_timers_process(ol_loop_t *loop) {
    uint64_t now = ol_monotonic_ms();
    for (ol_timer_t *t = *ol_loop_timers_head(loop); t; t = t->next) {
        if (!t->active) continue;
        if (now >= t->deadline_ms) {
            uint64_t start_ms = G_TRACE ? ol_monotonic_ms() : 0;

            /* Execute user callback */
            t->cb(t, t->arg);

            if (G_TRACE) {
                uint64_t dur = ol_monotonic_ms() - start_ms;
                /* Emit standardized trace entry */
                ol_trace_emit("timer", "tick", dur, OL_OK);
            }

            /* Reschedule or deactivate */
            if (t->period_ms) {
                /* For periodic timers, schedule relative to "now" to avoid drift accumulation */
                t->deadline_ms = now + t->period_ms;
            } else {
                t->active = 0;
            }
        }
    }
}
