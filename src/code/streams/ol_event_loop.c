#include "ol_event_loop.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>


/* -------------------------------
 * Internal data structures
 * ------------------------------- */

typedef struct {
    uint64_t    id;
    ol_ev_type_t type;

    /* IO fields */
    int         fd;
    uint32_t    mask;

    /* TIMER fields */
    int64_t     when_ns;       /* absolute deadline */
    int64_t     periodic_ns;   /* 0 for one-shot, >0 for periodic */

    /* Common */
    ol_event_cb cb;
    void       *user_data;
    bool        active;
} ol_loop_event_t;

struct ol_event_loop {
    bool          running;

    /* Wake mechanism: portable pipe; read-end monitored by poller, write-end for wake/stop */
    int           wake_rd;
    int           wake_wr;

    /* Poller abstraction (epoll/kqueue/select inside) */
    ol_poller_t  *poller;

    /* Registered events */
    ol_loop_event_t *events;
    size_t        count;
    size_t        capacity;

    /* Monotonic id generator (id=0 reserved) */
    uint64_t      next_id;
};

/* -------------------------------
 * Internal helpers
 * ------------------------------- */

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
    /* Most POSIX systems support pipe; fallback identical */
    if (pipe(fds) < 0) return -1;
    return 0;
#endif
}

static ol_loop_event_t* ol_find_event(ol_event_loop_t *loop, uint64_t id) {
    if (!loop || id == 0 || !loop->events) return NULL;
    for (size_t i = 0; i < loop->count; i++) {
        if (loop->events[i].active && loop->events[i].id == id) {
            return &loop->events[i];
        }
    }
    return NULL;
}

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

static int64_t ol_next_timer_deadline_ns(ol_event_loop_t *loop) {
    int64_t next_ns = 0; /* 0 means "no timer" */
    for (size_t i = 0; i < loop->count; i++) {
        ol_loop_event_t *e = &loop->events[i];
        if (!e->active || e->type != OL_EV_TIMER) continue;
        if (next_ns == 0 || e->when_ns < next_ns) next_ns = e->when_ns;
    }
    return next_ns;
}

static void ol_dispatch_due_timers(ol_event_loop_t *loop) {
    int64_t now = ol_monotonic_now_ns();
    for (size_t i = 0; i < loop->count; i++) {
        ol_loop_event_t *e = &loop->events[i];
        if (!e->active || e->type != OL_EV_TIMER) continue;
        if (now >= e->when_ns) {
            /* Fire */
            ol_event_cb cb = e->cb;
            void *ud = e->user_data;
            /* For safety, compute next before callback in case callback modifies the loop */
            bool periodic = e->periodic_ns > 0;
            int64_t next_when = periodic ? (now + e->periodic_ns) : 0;

            /* Invoke */
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

static int ol_register_wake_reader(ol_event_loop_t *loop) {
    /* Monitor wake_rd for readability to be able to interrupt wait */
    return ol_poller_add(loop->poller, loop->wake_rd, OL_POLL_IN, /*tag*/ 0);
}

static void ol_drain_wake_pipe(int fd) {
    char buf[64];
    while (read(fd, buf, sizeof(buf)) > 0) { /* drain */ }
}

/* -------------------------------
 * Public API
 * ------------------------------- */

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

int ol_event_loop_run(ol_event_loop_t *loop) {
    if (!loop) return -1;
    loop->running = true;

    /* Pre-allocate events buffer for poller wait */
    const int initial_cap = 64;
    ol_poll_event_t *pevs = (ol_poll_event_t*)malloc(sizeof(ol_poll_event_t) * initial_cap);
    if (!pevs) return -1;
    int pevs_cap = initial_cap;

    while (loop->running) {
        /* Compute next deadline: min of timers; if none, infinite wait */
        int64_t next_timer_ns = ol_next_timer_deadline_ns(loop);
        ol_deadline_t dl;
        if (next_timer_ns == 0) {
            dl.when_ns = 0; /* infinite wait */
        } else {
            dl.when_ns = next_timer_ns;
        }

        /* Wait for I/O or until next timer */
        int n = ol_poller_wait(loop->poller, dl, pevs, pevs_cap);

        if (n < 0) {
            if (errno == EINTR) continue;
            /* Treat as fatal; stop loop */
            loop->running = false;
            break;
        }

        if (n > pevs_cap) {
            /* Defensive: ensure buffer large enough (backend shouldn't exceed cap) */
            int new_cap = pevs_cap * 2;
            ol_poll_event_t *new_buf = (ol_poll_event_t*)realloc(pevs, sizeof(ol_poll_event_t) * new_cap);
            if (new_buf) { pevs = new_buf; pevs_cap = new_cap; }
        }

        /* Dispatch I/O events first */
        for (int i = 0; i < n; i++) {
            uint64_t tag = pevs[i].tag;
            uint32_t mask = pevs[i].mask;

            /* Wake pipe tagged as 0: drain and skip */
            if (tag == 0) {
                ol_drain_wake_pipe(loop->wake_rd);
                continue;
            }

            ol_loop_event_t *e = ol_find_event(loop, tag);
            if (!e || !e->active || e->type != OL_EV_IO) {
                continue;
            }

            /* Error/hup: still dispatch; callback decides what to do */
            (void)mask; /* mask available for future mask-aware callbacks */
            e->cb(loop, OL_EV_IO, e->fd, e->user_data);
        }

        /* Then handle due timers (they may enqueue work) */
        ol_dispatch_due_timers(loop);
    }

    free(pevs);
    return 0;
}

void ol_event_loop_stop(ol_event_loop_t *loop) {
    if (!loop) return;
    loop->running = false;
    (void)ol_event_loop_wake(loop);
}

int ol_event_loop_wake(ol_event_loop_t *loop) {
    if (!loop) return -1;
    /* Write a byte to wake pipe; ignore EAGAIN for non-blocking pipe */
    const char b = 1;
    ssize_t r = write(loop->wake_wr, &b, 1);
    return (r == 1) ? 0 : -1;
}

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

    /* If the timer is earlier than current wake, poke the loop to reconsider wait timeout */
    (void)ol_event_loop_wake(loop);
    return id;
}

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

bool ol_event_loop_is_running(const ol_event_loop_t *loop) {
    return loop && loop->running;
}

size_t ol_event_loop_event_count(const ol_event_loop_t *loop) {
    return loop ? loop->count : 0;
}
