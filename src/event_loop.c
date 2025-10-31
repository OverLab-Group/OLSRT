/* OLSRT - Event loop */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>

/* Forward declaration from timers section */
struct ol_timer_s;
static void ol_timers_process(ol_loop_t *l);

/* Task node (FIFO queue) */
typedef struct ol_task_node_s {
    ol_task_fn fn;
    void      *arg;
    struct ol_task_node_s *next;
} ol_task_node_t;

/* Loop object */
struct ol_loop_s {
    ol_loop_opts_t  opts;
    int             running;
    ol_poller_t    *poller;
    ol_task_node_t *head;
    ol_task_node_t *tail;
    struct ol_timer_s *timers; /* linked list head of timers */
};

/* Internal: enqueue a task at the tail */
static void ol_enqueue_task(ol_loop_t *loop, ol_task_fn fn, void *arg) {
    ol_task_node_t *n = (ol_task_node_t*)malloc(sizeof(*n));
    if (!n) return; /* best-effort; caller cannot recover without hook */
    n->fn  = fn;
    n->arg = arg;
    n->next= NULL;
    if (!loop->tail) {
        loop->head = loop->tail = n;
    } else {
        loop->tail->next = n;
        loop->tail       = n;
    }
}

/* Internal: drain the task queue (one batch per tick) */
static void ol_process_tasks(ol_loop_t *loop) {
    ol_task_node_t *n = loop->head;
    loop->head = loop->tail = NULL;
    while (n) {
        ol_task_node_t *next = n->next;
        n->fn(n->arg);
        free(n);
        n = next;
    }
}

/* Create a loop with options (or defaults) and attach a poller backend */
ol_loop_t* ol_loop_create(const ol_loop_opts_t *opts) {
    ol_loop_t *l = (ol_loop_t*)calloc(1, sizeof(*l));
    if (!l) return NULL;

    if (opts) {
        l->opts = *opts;
    } else {
        l->opts.enable_debug   = 0;
        l->opts.allow_blocking = 1;
        l->opts.max_events     = 1024;
        l->opts.poller_hint    = 0; /* auto */
    }

    l->poller  = ol_poller_create(l->opts.poller_hint, l->opts.max_events);
    l->running = 0;
    l->head    = NULL;
    l->tail    = NULL;
    l->timers  = NULL;

    if (!l->poller) { free(l); return NULL; }
    return l;
}

/* Destroy the loop, releasing poller and any queued tasks */
void ol_loop_destroy(ol_loop_t *loop) {
    if (!loop) return;
    if (loop->poller) ol_poller_destroy(loop->poller);

    /* Free remaining tasks */
    ol_task_node_t *n = loop->head;
    while (n) {
        ol_task_node_t *next = n->next;
        free(n);
        n = next;
    }

    /* Timers will be freed by their own lifecycle; loop holds only references */
    free(loop);
}

/* Post a task to be executed on the next tick */
int ol_loop_post(ol_loop_t *loop, ol_task_fn fn, void *arg) {
    if (!loop || !fn) return OL_ERR_STATE;
    ol_enqueue_task(loop, fn, arg);
    return OL_OK;
}

/* Single tick: run queued tasks, process timers, pump poller */
int ol_loop_tick(ol_loop_t *loop) {
    if (!loop) return OL_ERR_STATE;

    /* Tasks first (fast-path callbacks, user scheduling, I/O completions) */
    ol_process_tasks(loop);

    /* Timers second (callbacks may schedule more tasks) */
    ol_timers_process(loop);

    /* Poller last: pump ready I/O; short timeout keeps loop responsive */
    (void)ol_poller_wait(loop->poller, 10);

    return OL_OK;
}

/* Run the loop until stopped */
int ol_loop_run(ol_loop_t *loop) {
    if (!loop) return OL_ERR_STATE;
    loop->running = 1;
    while (loop->running) {
        int rc = ol_loop_tick(loop);
        if (rc != OL_OK && !loop->opts.allow_blocking) {
            /* In non-blocking mode, break on error to avoid tight spin */
            break;
        }
    }
    return OL_OK;
}

/* Signal the loop to stop after current tick */
int ol_loop_stop(ol_loop_t *loop) {
    if (!loop) return OL_ERR_STATE;
    loop->running = 0;
    return OL_OK;
}
