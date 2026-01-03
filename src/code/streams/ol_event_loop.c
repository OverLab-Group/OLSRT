/**
 * @file ol_event_loop.c
 * @brief Portable event loop with I/O and timer support.
 *
 * This module implements a compact event loop intended to run on a single
 * thread. It supports:
 *  - registration/unregistration of I/O events (fd + mask)
 *  - registration/unregistration of timers (one-shot and periodic)
 *  - a wake mechanism (pipe) to interrupt blocking poll from other threads
 *  - a dynamic registry of events with monotonic unique IDs
 *
 * The poller backend (ol_poller_t) is an abstraction that must provide:
 *  - ol_poller_create / ol_poller_destroy
 *  - ol_poller_add(fd, mask, tag)
 *  - ol_poller_mod(fd, mask, tag)
 *  - ol_poller_del(fd)
 *  - ol_poller_wait(deadline, out_events, capacity) -> n_events
 *
 * Thread-safety:
 *  - The loop is designed to be driven from one thread. ol_event_loop_wake()
 *    is safe to call from other threads. Registration/unregistration may be
 *    called from other threads but callers must ensure user_data and fds are
 *    not concurrently freed without synchronization.
 *
 * Timers:
 *  - Timers use monotonic deadlines (ol_deadline_t) to avoid wall-clock jumps.
 *  - periodic_ns > 0 makes a timer periodic; otherwise it is one-shot.
 */

#include "ol_event_loop.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* --------------------------------------------------------------------------
 * Internal types
 * -------------------------------------------------------------------------- */

/**
 * @brief Internal representation of a registered event.
 *
 * Each entry represents either an I/O registration or a timer.
 *
 * Fields:
 *  - id: unique non-zero identifier returned to callers (0 reserved).
 *  - type: OL_EV_IO or OL_EV_TIMER.
 *  - fd/mask: used for I/O registrations.
 *  - when_ns/periodic_ns: used for timers (absolute monotonic ns).
 *  - cb/user_data: callback and user context invoked on event.
 *  - active: whether this entry is currently active.
 */
typedef struct {
    uint64_t    id;
    ol_ev_type_t type;

    /* I/O fields */
    int         fd;
    uint32_t    mask;

    /* TIMER fields */
    int64_t     when_ns;       /* absolute deadline (monotonic ns) */
    int64_t     periodic_ns;   /* 0 => one-shot, >0 => periodic interval in ns */

    /* Common */
    ol_event_cb cb;
    void       *user_data;
    bool        active;
} ol_loop_event_t;

/**
 * @brief Event loop state structure.
 *
 * Ownership and lifetime:
 *  - The loop owns the poller and the wake pipe fds.
 *  - The loop stores user_data pointers but does not manage their memory.
 *  - Callbacks run on the loop thread and must not free user_data concurrently
 *    with registration/unregistration from other threads unless externally synchronized.
 */
struct ol_event_loop {
    bool          running;

    /* Wake mechanism: portable pipe; read-end monitored by poller, write-end for wake/stop */
    int           wake_rd;
    int           wake_wr;

    /* Poller abstraction (epoll/kqueue/select inside) */
    ol_poller_t  *poller;

    /* Registered events */
    ol_loop_event_t *events;   /* dynamic array */
    size_t        count;       /* number of entries in events[] (active + inactive) */
    size_t        capacity;    /* allocated capacity */

    /* Monotonic id generator (id=0 reserved) */
    uint64_t      next_id;
};

/* --------------------------------------------------------------------------
 * Internal helpers (static)
 * -------------------------------------------------------------------------- */

/**
 * @brief Create a pipe and set non-blocking flags if possible.
 *
 * On POSIX-like systems attempts to set O_NONBLOCK on both ends.
 *
 * @param fds[out] array of two ints; fds[0] = read end, fds[1] = write end.
 * @return 0 on success, -1 on failure (errno set by pipe/fcntl).
 */
static int ol_make_pipe(int fds[2]) {
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    #include <fcntl.h>
    if (pipe(fds) < 0) return -1;
    /* Best-effort set O_NONBLOCK */
    int flags;
    flags = fcntl(fds[0], F_GETFL, 0); if (flags != -1) (void)fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(fds[1], F_GETFL, 0); if (flags != -1) (void)fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);
    return 0;
#else
    /* Fallback: create pipe without non-blocking flags */
    if (pipe(fds) < 0) return -1;
    return 0;
#endif
}

/**
 * @brief Find an active event entry by id.
 *
 * Linear scan over events[]; acceptable because registry is expected to be
 * moderate in size. Returns pointer to entry or NULL if not found.
 *
 * @param loop Event loop pointer (non-NULL).
 * @param id Event id to find (non-zero).
 * @return pointer to ol_loop_event_t or NULL.
 */
static ol_loop_event_t* ol_find_event(ol_event_loop_t *loop, uint64_t id) {
    if (!loop || id == 0 || !loop->events) return NULL;
    for (size_t i = 0; i < loop->count; i++) {
        if (loop->events[i].active && loop->events[i].id == id) {
            return &loop->events[i];
        }
    }
    return NULL;
}

/**
 * @brief Ensure events[] has capacity for at least 'want' entries.
 *
 * Grows by doubling strategy; returns 0 on success.
 *
 * @param loop Event loop pointer.
 * @param want Minimum required capacity (count + 1 typically).
 * @return 0 on success, -1 on allocation failure.
 */
static int ol_ensure_capacity(ol_event_loop_t *loop, size_t want) {
    if (want <= loop->capacity) return 0;
    size_t new_cap = loop->capacity ? loop->capacity * 2 : 16;
    if (new_cap < want) new_cap = want;
    ol_loop_event_t *new_events = (ol_loop_event_t*)realloc(loop->events, new_cap * sizeof(ol_loop_event_t));
    if (!new_events) return -1;
    loop->events = new_events;
    loop->capacity = new_cap;
    return 0;
}

/**
 * @brief Compact the events[] array by removing inactive entries.
 *
 * Moves active entries to the front and optionally shrinks the allocation.
 * After compaction loop->count is set to the number of active entries.
 *
 * @param loop Event loop pointer.
 */
static void ol_compact_events(ol_event_loop_t *loop) {
    size_t w = 0;
    for (size_t r = 0; r < loop->count; r++) {
        if (loop->events[r].active) {
            if (w != r) loop->events[w] = loop->events[r];
            w++;
        }
    }
    if (w == 0) {
        free(loop->events);
        loop->events = NULL;
        loop->capacity = 0;
    } else if (w < loop->capacity / 2) {
        ol_loop_event_t *shrunk = (ol_loop_event_t*)realloc(loop->events, w * sizeof(ol_loop_event_t));
        if (shrunk) {
            loop->events = shrunk;
            loop->capacity = w;
        }
    }
    loop->count = w;
}

/**
 * @brief Return the earliest timer deadline among active timers.
 *
 * Scans events[] for OL_EV_TIMER entries and returns the minimum when_ns.
 * Returns 0 if no timers are registered (meaning infinite wait).
 *
 * @param loop Event loop pointer.
 * @return absolute monotonic ns of next timer, or 0 if none.
 */
static int64_t ol_next_timer_deadline_ns(ol_event_loop_t *loop) {
    int64_t next_ns = 0; /* 0 means "no timer" */
    for (size_t i = 0; i < loop->count; i++) {
        ol_loop_event_t *e = &loop->events[i];
        if (!e->active || e->type != OL_EV_TIMER) continue;
        if (next_ns == 0 || e->when_ns < next_ns) next_ns = e->when_ns;
    }
    return next_ns;
}

/**
 * @brief Dispatch timers whose deadline has passed.
 *
 * For each due timer:
 *  - compute next deadline if periodic (before invoking callback)
 *  - invoke callback on loop thread: cb(loop, OL_EV_TIMER, -1, user_data)
 *  - if one-shot, mark entry inactive
 *
 * After processing, compacts the registry to remove inactive timers.
 *
 * @param loop Event loop pointer.
 */
static void ol_dispatch_due_timers(ol_event_loop_t *loop) {
    int64_t now = ol_monotonic_now_ns();
    for (size_t i = 0; i < loop->count; i++) {
        ol_loop_event_t *e = &loop->events[i];
        if (!e->active || e->type != OL_EV_TIMER) continue;
        if (now >= e->when_ns) {
            /* Fire */
            ol_event_cb cb = e->cb;
            void *ud = e->user_data;
            /* Compute next before callback in case callback modifies loop */
            bool periodic = e->periodic_ns > 0;
            int64_t next_when = periodic ? (now + e->periodic_ns) : 0;

            /* Invoke callback on loop thread. Callback must be robust to reentrancy. */
            cb(loop, OL_EV_TIMER, -1, ud);

            if (periodic) {
                e->when_ns = next_when;
            } else {
                e->active = false;
            }
        }
    }
    /* Remove inactive one-shot timers */
    ol_compact_events(loop);
}

/**
 * @brief Register the wake pipe read-end with the poller.
 *
 * The wake reader is registered with tag 0 (reserved). This allows the poller
 * to return an event with tag==0 when the wake pipe becomes readable.
 *
 * @param loop Event loop pointer.
 * @return 0 on success, -1 on failure.
 */
static int ol_register_wake_reader(ol_event_loop_t *loop) {
    return ol_poller_add(loop->poller, loop->wake_rd, OL_POLL_IN, /*tag*/ 0);
}

/**
 * @brief Drain the wake pipe read-end.
 *
 * Reads and discards bytes until EAGAIN/EWOULDBLOCK or EOF. Best-effort.
 *
 * @param fd read-end of wake pipe.
 */
static void ol_drain_wake_pipe(int fd) {
    char buf[64];
    while (read(fd, buf, sizeof(buf)) > 0) { /* drain */ }
}

/* --------------------------------------------------------------------------
 * Public API (documented)
 * -------------------------------------------------------------------------- */

/**
 * @brief Create a new event loop instance.
 *
 * Allocates and initializes:
 *  - ol_event_loop_t structure
 *  - poller backend via ol_poller_create()
 *  - wake pipe (non-blocking if possible)
 *  - registers wake reader with poller
 *
 * Ownership:
 *  - Caller owns returned pointer and must call ol_event_loop_destroy().
 *
 * @return pointer to new ol_event_loop_t on success, NULL on failure.
 */
ol_event_loop_t* ol_event_loop_create(void) {
    ol_event_loop_t *loop = (ol_event_loop_t*)calloc(1, sizeof(ol_event_loop_t));
    if (!loop) return NULL;

    loop->poller = ol_poller_create();
    if (!loop->poller) { free(loop); return NULL; }

    int fds[2];
    if (ol_make_pipe(fds) != 0) {
        ol_poller_destroy(loop->poller);
        free(loop);
        return NULL;
    }

    loop->wake_rd = fds[0];
    loop->wake_wr = fds[1];
    loop->running = false;
    loop->events = NULL;
    loop->count = 0;
    loop->capacity = 0;
    loop->next_id = 1;

    /* Register wake reader with tag=0 (reserved) */
    if (ol_register_wake_reader(loop) != 0) {
        close(loop->wake_rd);
        close(loop->wake_wr);
        ol_poller_destroy(loop->poller);
        free(loop);
        return NULL;
    }

    return loop;
}

/**
 * @brief Destroy an event loop and free all resources.
 *
 * Behavior:
 *  - Unregisters all I/O fds from poller (best-effort).
 *  - Removes wake reader and closes wake pipe fds.
 *  - Frees events array and destroys poller.
 *
 * Note:
 *  - Does not invoke callbacks or free user_data pointers.
 *
 * @param loop Pointer returned by ol_event_loop_create (may be NULL).
 */
void ol_event_loop_destroy(ol_event_loop_t *loop) {
    if (!loop) return;

    /* Unregister all I/O fds from poller */
    for (size_t i = 0; i < loop->count; i++) {
        ol_loop_event_t *e = &loop->events[i];
        if (!e->active) continue;
        if (e->type == OL_EV_IO && e->fd >= 0) {
            (void)ol_poller_del(loop->poller, e->fd);
        }
        e->active = false;
    }

    /* Remove wake reader */
    (void)ol_poller_del(loop->poller, loop->wake_rd);

    /* Close wake pipe */
    if (loop->wake_rd >= 0) close(loop->wake_rd);
    if (loop->wake_wr >= 0) close(loop->wake_wr);

    /* Free events array */
    free(loop->events);

    /* Destroy poller */
    ol_poller_destroy(loop->poller);

    /* Free loop */
    free(loop);
}

/**
 * @brief Run the event loop until stopped.
 *
 * Loop behavior:
 *  - Compute next timer deadline and call ol_poller_wait with that deadline.
 *  - Dispatch I/O events returned by poller (I/O callbacks first).
 *  - Drain wake pipe when wake event (tag==0) is observed.
 *  - After I/O dispatch, call ol_dispatch_due_timers to run due timers.
 *
 * Return values:
 *  - 0 on normal exit (loop stopped)
 *  - -1 on invalid argument
 *
 * @param loop Event loop pointer.
 * @return 0 on success, -1 on invalid argument.
 */
int ol_event_loop_run(ol_event_loop_t *loop) {
    if (!loop) return -1;
    loop->running = true;

    /* Pre-allocate events buffer for poller wait */
    const int initial_cap = 256; /* grow default to reduce realloc churn */
    ol_poll_event_t *pevs = (ol_poll_event_t*)malloc(sizeof(ol_poll_event_t) * initial_cap);
    if (!pevs) return -1;
    int pevs_cap = initial_cap;

    while (loop->running) {
        /* Compute next deadline: min of timers; if none, infinite wait */
        int64_t next_timer_ns = ol_next_timer_deadline_ns(loop);
        ol_deadline_t dl;
        dl.when_ns = (next_timer_ns == 0) ? 0 : next_timer_ns;

        /* Wait for I/O or until next timer */
        int n = ol_poller_wait(loop->poller, dl, pevs, pevs_cap);

        if (n < 0) {
            if (errno == EINTR) continue;
            /* Treat as fatal; stop loop */
            loop->running = false;
            break;
        }

        /* Defensive: backend should not exceed cap; if it asks for more, grow for next iteration */
        if (n > pevs_cap) {
            int new_cap = pevs_cap * 2;
            ol_poll_event_t *new_buf = (ol_poll_event_t*)realloc(pevs, sizeof(ol_poll_event_t) * new_cap);
            if (new_buf) { pevs = new_buf; pevs_cap = new_cap; }
            /* Process only up to current cap; overflow events will be picked up next tick.
             * This maintains correctness without risking buffer overrun. */
            n = pevs_cap;
        }

        /* Dispatch I/O events first */
        for (int i = 0; i < n; i++) {
            uint64_t tag = pevs[i].tag;

            /* Wake pipe tagged as 0: drain and skip */
            if (tag == 0) {
                ol_drain_wake_pipe(loop->wake_rd);
                continue;
            }

            ol_loop_event_t *e = ol_find_event(loop, tag);
            if (!e || !e->active || e->type != OL_EV_IO) {
                continue;
            }

            /* Invoke I/O callback on loop thread. Callback must be robust to reentrancy. */
            e->cb(loop, OL_EV_IO, e->fd, e->user_data);
        }

        /* Then handle due timers (they may enqueue work) */
        ol_dispatch_due_timers(loop);
    }

    free(pevs);
    return 0;
}

/**
 * @brief Stop the event loop.
 *
 * Sets running=false and wakes the loop so that ol_event_loop_run returns.
 *
 * @param loop Event loop pointer.
 */
void ol_event_loop_stop(ol_event_loop_t *loop) {
    if (!loop) return;
    loop->running = false;
    (void)ol_event_loop_wake(loop);
}

/**
 * @brief Wake the event loop from another thread.
 *
 * Writes a single byte to the wake pipe write-end. The loop monitors the
 * read-end and will be interrupted from poll. Best-effort: ignores EAGAIN.
 *
 * @param loop Event loop pointer.
 * @return 0 on success (byte written), -1 on error.
 */
int ol_event_loop_wake(ol_event_loop_t *loop) {
    if (!loop) return -1;
    /* Write a byte to wake pipe; ignore EAGAIN for non-blocking pipe */
    const char b = 1;
    ssize_t r = write(loop->wake_wr, &b, 1);
    return (r == 1) ? 0 : -1;
}

/**
 * @brief Register an I/O event with the loop.
 *
 * Adds fd to the poller with the provided mask and associates a callback.
 * The returned id is unique and can be used to modify or unregister the event.
 *
 * Ownership/semantics:
 *  - The loop stores user_data pointer but does not manage its memory.
 *  - Caller must ensure fd remains valid until unregistered.
 *
 * @param loop Event loop pointer.
 * @param fd File descriptor to monitor (>=0).
 * @param mask Poll mask (OL_POLL_IN / OL_POLL_OUT).
 * @param cb Callback invoked when fd is ready: cb(loop, OL_EV_IO, fd, user_data).
 * @param user_data Opaque pointer passed to callback.
 * @return unique event id (>0) on success, 0 on failure.
 */
uint64_t ol_event_loop_register_io(ol_event_loop_t *loop, int fd, uint32_t mask, ol_event_cb cb, void *user_data) {
    if (!loop || fd < 0 || !cb) return 0;

    uint64_t id = loop->next_id++;
    if (ol_ensure_capacity(loop, loop->count + 1) != 0) return 0;

    /* Add to poller with tag=id */
    if (ol_poller_add(loop->poller, fd, mask, id) != 0) {
        return 0;
    }

    ol_loop_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.id     = id;
    ev.type   = OL_EV_IO;
    ev.fd     = fd;
    ev.mask   = mask;
    ev.cb     = cb;
    ev.user_data = user_data;
    ev.active = true;

    loop->events[loop->count++] = ev;
    return id;
}

/**
 * @brief Modify the mask for a registered I/O event.
 *
 * Updates the poller mask for the fd associated with id.
 *
 * @param loop Event loop pointer.
 * @param id Event id returned by ol_event_loop_register_io.
 * @param mask New poll mask.
 * @return 0 on success, -1 on error (invalid id or poller failure).
 */
int ol_event_loop_mod_io(ol_event_loop_t *loop, uint64_t id, uint32_t mask) {
    if (!loop || id == 0) return -1;
    ol_loop_event_t *e = ol_find_event(loop, id);
    if (!e || e->type != OL_EV_IO) return -1;

    if (ol_poller_mod(loop->poller, e->fd, mask, id) != 0) {
        return -1;
    }
    e->mask = mask;
    return 0;
}

/**
 * @brief Register a timer event.
 *
 * If deadline.when_ns <= 0 the timer is scheduled immediately (now).
 * periodic_ns > 0 makes the timer periodic; otherwise it is one-shot.
 *
 * Callback signature: cb(loop, OL_EV_TIMER, -1, user_data)
 *
 * @param loop Event loop pointer.
 * @param deadline Absolute monotonic deadline (ol_deadline_t).
 * @param periodic_ns Period in ns for periodic timers (0 for one-shot).
 * @param cb Callback invoked when timer fires.
 * @param user_data Opaque pointer passed to callback.
 * @return unique event id (>0) on success, 0 on failure.
 */
uint64_t ol_event_loop_register_timer(ol_event_loop_t *loop, ol_deadline_t deadline, int64_t periodic_ns, ol_event_cb cb, void *user_data) {
    if (!loop || !cb) return 0;

    uint64_t id = loop->next_id++;
    if (ol_ensure_capacity(loop, loop->count + 1) != 0) return 0;

    if (deadline.when_ns <= 0) {
        /* If no deadline specified, schedule immediately */
        deadline.when_ns = ol_monotonic_now_ns();
    }

    ol_loop_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.id       = id;
    ev.type     = OL_EV_TIMER;
    ev.fd       = -1;
    ev.when_ns  = deadline.when_ns;
    ev.periodic_ns = (periodic_ns > 0) ? periodic_ns : 0;
    ev.cb       = cb;
    ev.user_data = user_data;
    ev.active   = true;

    loop->events[loop->count++] = ev;

    /* If the timer is earlier than current wait, poke the loop to reconsider wait timeout */
    (void)ol_event_loop_wake(loop);
    return id;
}

/**
 * @brief Unregister an event by id.
 *
 * For I/O events the fd is removed from the poller. The event entry is marked
 * inactive and the registry is compacted.
 *
 * @param loop Event loop pointer.
 * @param id Event id to remove.
 * @return 0 on success, -1 on error (invalid id).
 */
int ol_event_loop_unregister(ol_event_loop_t *loop, uint64_t id) {
    if (!loop || id == 0) return -1;
    ol_loop_event_t *e = ol_find_event(loop, id);
    if (!e) return -1;

    if ( e->type == OL_EV_IO && e->fd >= 0 ) {
        (void)ol_poller_del(loop->poller, e->fd);
    }
    e->active = false;
    ol_compact_events(loop);
    return 0;
}

/**
 * @brief Check whether the event loop is running.
 *
 * @param loop Event loop pointer.
 * @return true if running, false otherwise.
 */
bool ol_event_loop_is_running(const ol_event_loop_t *loop) {
    return loop && loop->running;
}

/**
 * @brief Get number of registered events (active entries).
 *
 * @param loop Event loop pointer.
 * @return number of active events, or 0 if loop is NULL.
 */
size_t ol_event_loop_event_count(const ol_event_loop_t *loop) {
    return loop ? loop->count : 0;
}