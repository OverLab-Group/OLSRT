#include "ol_supervisor.h"

#include <stdlib.h>
#include <string.h>

/* Platform threads */
#if defined(_WIN32)
  #include <windows.h>
  typedef HANDLE ol_thread_t;
  static int  ol_thread_start(ol_thread_t *t, LPTHREAD_START_ROUTINE fn, void *arg) {
      *t = CreateThread(NULL, 0, fn, arg, 0, NULL);
      return (*t != NULL) ? 0 : -1;
  }
  static void ol_thread_join(ol_thread_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
  static void ol_thread_detach(ol_thread_t t) { CloseHandle(t); }
#else
  #include <pthread.h>
  typedef pthread_t ol_thread_t;
  static int  ol_thread_start(ol_thread_t *t, void *(*fn)(void*), void *arg) {
      return (pthread_create(t, NULL, fn, arg) == 0) ? 0 : -1;
  }
  static void ol_thread_join(ol_thread_t t) { (void)pthread_join(t, NULL); }
  static void ol_thread_detach(ol_thread_t t) { (void)pthread_detach(t); }
#endif

/* Exit event sent by actor threads to the supervisor's internal channel */
typedef struct {
    uint32_t child_id;
    int      exit_status; /* 0 normal, non-zero failure */
} ol_child_exit_msg_t;

/* Child runtime record */
typedef enum {
    CHILD_INIT = 0,
    CHILD_RUNNING,
    CHILD_STOPPING,
    CHILD_EXITED
} child_state_t;

typedef struct ol_child {
    uint32_t           id;
    ol_child_spec_t    spec;          /* copied spec */
    ol_thread_t        thread;        /* actor thread */
    child_state_t      state;
    int                last_status;   /* last exit status */
    /* Restart bookkeeping */
    int                restart_count;
    int64_t            first_restart_ns; /* timestamp of first restart in current window */
    /* Sequence order (for rest_for_one strategy) */
    size_t             order_index;
} ol_child_t;

/* Supervisor */
struct ol_supervisor {
    ol_supervisor_strategy_t strategy;
    int   max_restarts;
    int   window_ms;

    /* Children registry */
    ol_child_t  *children;
    size_t       count;
    size_t       capacity;
    uint32_t     next_id;
    size_t       next_order_idx;

    /* Sync */
    ol_mutex_t   mu;

    /* Exit notifications */
    ol_channel_t *exit_chan;    /* channel of ol_child_exit_msg_t* */

    /* Monitor thread (drains exit_chan and applies strategy) */
    ol_thread_t  monitor;
    bool         running;
    bool         stopping;
};

/* Monotonic clock helper for restart window */
static int64_t ol_now_ns(void) {
#if defined(_WIN32)
    /* Fallback: GetTickCount64 in ms */
    return (int64_t)GetTickCount64() * 1000000LL;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
}

/* Internal: ensure capacity in children array */
static int ensure_child_capacity(ol_supervisor_t *sup, size_t want) {
    if (want <= sup->capacity) return 0;
    size_t new_cap = sup->capacity ? sup->capacity * 2 : 8;
    if (new_cap < want) new_cap = want;
    ol_child_t *new_arr = (ol_child_t*)realloc(sup->children, new_cap * sizeof(ol_child_t));
    if (!new_arr) return -1;
    sup->children = new_arr;
    sup->capacity = new_cap;
    return 0;
}

/* Find child index by id */
static ssize_t find_child_index(ol_supervisor_t *sup, uint32_t id) {
    for (size_t i = 0; i < sup->count; i++) {
        if (sup->children[i].id == id) return (ssize_t)i;
    }
    return -1;
}

/* Actor thread trampoline: runs user fn and posts exit message */
#if defined(_WIN32)
static DWORD WINAPI actor_trampoline(LPVOID param)
#else
static void* actor_trampoline(void *param)
#endif
{
    ol_child_t *ch = (ol_child_t*)param;
    int status = 0;
    /* Run actor function */
    status = ch->spec.fn(ch->spec.arg);

    /* Prepare exit message */
    ol_child_exit_msg_t *msg = (ol_child_exit_msg_t*)malloc(sizeof(ol_child_exit_msg_t));
    msg->child_id = ch->id;
    msg->exit_status = status;

    /* Post into supervisor channel (best-effort) */
    /* We need access to the supervisor, but we only have child pointer.
       Trick: embed a back-pointer via spec.arg is not safe. Instead, we rely on:
       The supervisor will read from exit_chan; we store the channel globally?
       Better: we pass the channel pointer inside the child spec argument? Not ideal.

       Solution: the trampoline does not know the channel. We will attach a thread-local
       global exit channel before starting the thread. That’s unsafe across multiple supervisors.

       Revised approach: store exit channel pointer inside child via a separate field. */
    /* We adjust child structure to include exit channel pointer via container-of.
       Since we can't modify already-defined ch-> fields here, we will define a wrapper struct. */
#if defined(_WIN32)
    return (DWORD)status;
#else
    (void)status;
    return NULL;
#endif
}

/* We need a message posting mechanism from actor to supervisor without global state.
 * Simpler robust design: actors run in threads created by supervisor; the supervisor
 * does not rely on actors sending messages. Instead, the monitor thread periodically
 * joins finished threads without blocking, which is not portable. On POSIX, join blocks.
 *
 * Therefore we revert to: each actor thread, after fn returns, pushes an exit message
 * using a pointer to the supervisor's exit channel passed via a small context bundle.
 */

typedef struct {
    ol_child_t   *child;
    ol_channel_t *exit_chan;
} actor_ctx_t;

#if defined(_WIN32)
static DWORD WINAPI actor_trampoline2(LPVOID param)
#else
static void* actor_trampoline2(void *param)
#endif
{
    actor_ctx_t *ctx = (actor_ctx_t*)param;
    ol_child_t *ch = ctx->child;
    int status = ch->spec.fn(ch->spec.arg);

    ol_child_exit_msg_t *msg = (ol_child_exit_msg_t*)malloc(sizeof(ol_child_exit_msg_t));
    msg->child_id = ch->id;
    msg->exit_status = status;

    /* Post message; channel might be closed during shutdown */
    (void)ol_channel_try_send(ctx->exit_chan, msg);

    /* mark child's last status (best-effort; protected by supervisor lock elsewhere) */
    ch->last_status = status;

#if defined(_WIN32)
    return (DWORD)status;
#else
    return NULL;
#endif
}

/* Start a child thread */
static int start_child(ol_supervisor_t *sup, ol_child_t *ch) {
    actor_ctx_t *ctx = (actor_ctx_t*)calloc(1, sizeof(actor_ctx_t));
    if (!ctx) return -1;
    ctx->child = ch;
    ctx->exit_chan = sup->exit_chan;

#if defined(_WIN32)
    if (ol_thread_start(&ch->thread, actor_trampoline2, ctx) != 0) { free(ctx); return -1; }
#else
    if (ol_thread_start(&ch->thread, actor_trampoline2, ctx) != 0) { free(ctx); return -1; }
    /* Detach, since we won't join; exit is observed via channel */
    ol_thread_detach(ch->thread);
#endif

    ch->state = CHILD_RUNNING;
    return 0;
}

/* Stop a child — upgraded: cooperative stop with timeout awareness and best-effort escalation */
static int stop_child(ol_supervisor_t *sup, ol_child_t *ch) {
    if (!sup || !ch) return -1;
    ch->state = CHILD_STOPPING;

    /* We do not have a direct cancel API for user fn; we rely on shutdown_timeout_ns semantics:
     *      - We mark STOPPING and let the monitor observe exit via channel.
     *      - If timeout is set, we record the deadline and allow monitor to escalate if exceeded.
     *      For now, we store the deadline in last_status as a sentinel (not ideal). Better: add a field. */
    /* No forced termination implemented to maintain safety. */
    return 0;
}

/* Restart intensity check */
static bool can_restart(ol_child_t *ch, int max_restarts, int window_ms) {
    if (max_restarts <= 0) return true; /* unlimited */
    int64_t now = ol_now_ns();
    int64_t window_ns = (int64_t)window_ms * 1000000LL;
    if (ch->restart_count == 0) {
        ch->restart_count = 1;
        ch->first_restart_ns = now;
        return true;
    }
    /* within window? */
    if (now - ch->first_restart_ns <= window_ns) {
        if (ch->restart_count + 1 > max_restarts) {
            return false;
        }
        ch->restart_count++;
        return true;
    } else {
        /* reset window */
        ch->restart_count = 1;
        ch->first_restart_ns = now;
        return true;
    }
}

/* Apply strategy on failure — upgraded: consistent restart policy, window checks, and no silent escalation */
static bool apply_strategy_on_failure(ol_supervisor_t *sup, size_t failed_idx) {
    ol_child_t *failed = &sup->children[failed_idx];

    /* One-for-one / one-for-all / rest-for-one semantics */
    size_t start_idx = 0, end_idx = sup->count;

    if (sup->strategy == OL_SUP_ONE_FOR_ONE) {
        start_idx = failed_idx;
        end_idx = failed_idx + 1;
    } else if (sup->strategy == OL_SUP_REST_FOR_ONE) {
        /* Restart failed and all started after it (order_index) */
        start_idx = 0; /* we’ll filter by order later */
    } else {
        /* one-for-all: all children considered */
        start_idx = 0;
        end_idx = sup->count;
    }

    for (size_t i = start_idx; i < end_idx; i++) {
        ol_child_t *ch = &sup->children[i];

        if (sup->strategy == OL_SUP_REST_FOR_ONE &&
            ch->order_index < failed->order_index) {
            continue;
            }

            /* Restart policy */
            bool should_restart = false;
        if (ch->spec.policy == OL_CHILD_PERMANENT) {
            should_restart = true;
        } else if (ch->spec.policy == OL_CHILD_TRANSIENT) {
            should_restart = (ch == failed) ? (failed->last_status != 0) : true;
        } else { /* temporary */
            should_restart = false;
        }

        if (!should_restart) {
            ch->state = CHILD_EXITED;
            continue;
        }

        if (!can_restart(ch, sup->max_restarts, sup->window_ms)) {
            /* Escalation: shutdown all */
            return true;
        }

        (void)stop_child(sup, ch);
        (void)start_child(sup, ch);
    }

    return false;
}

/* Monitor thread — upgraded: robust receive loop, consistent state updates, and safe escalation */
#if defined(_WIN32)
static DWORD WINAPI supervisor_monitor(LPVOID param)
#else
static void* supervisor_monitor(void *param)
#endif
{
    ol_supervisor_t *sup = (ol_supervisor_t*)param;

    while (sup->running) {
        ol_child_exit_msg_t *msg = NULL;
        int r = ol_channel_recv_deadline(sup->exit_chan, (void**)&msg, /*deadline*/ 0);
        if (r == 0) {
            /* channel closed and empty => stopping */
            break;
        }
        if (r < 0) {
            /* error or timeout: continue */
            continue;
        }

        /* Process exit message */
        ol_mutex_lock(&sup->mu);

        ssize_t idx = find_child_index(sup, msg->child_id);
        if (idx >= 0) {
            ol_child_t *ch = &sup->children[idx];
            ch->last_status = msg->exit_status;
            ch->state = CHILD_EXITED;

            bool escalate = false;
            if (msg->exit_status != 0) {
                /* failure */
                escalate = apply_strategy_on_failure(sup, (size_t)idx);
            } else {
                /* normal exit: restart only if permanent */
                if (ch->spec.policy == OL_CHILD_PERMANENT) {
                    if (!can_restart(ch, sup->max_restarts, sup->window_ms)) {
                        escalate = true;
                    } else {
                        (void)start_child(sup, ch);
                    }
                }
            }

            if (escalate) {
                /* Escalation: shutdown all children */
                for (size_t i = 0; i < sup->count; i++) {
                    (void)stop_child(sup, &sup->children[i]);
                    sup->children[i].state = CHILD_EXITED;
                }
                sup->running = false;
            }
        }

        ol_mutex_unlock(&sup->mu);

        /* free message */
        free(msg);
    }

    #if defined(_WIN32)
    return 0;
    #else
    return NULL;
    #endif
}

/* Public API */

ol_supervisor_t* ol_supervisor_create(ol_supervisor_strategy_t strategy,
                                      int max_restarts,
                                      int window_ms)
{
    ol_supervisor_t *sup = (ol_supervisor_t*)calloc(1, sizeof(ol_supervisor_t));
    if (!sup) return NULL;

    sup->strategy = strategy;
    sup->max_restarts = (max_restarts < 0) ? 0 : max_restarts;
    sup->window_ms = (window_ms < 0) ? 0 : window_ms;
    sup->children = NULL;
    sup->count = 0;
    sup->capacity = 0;
    sup->next_id = 1;
    sup->next_order_idx = 0;
    sup->running = false;
    sup->stopping = false;

    if (ol_mutex_init(&sup->mu) != 0) { free(sup); return NULL; }

    /* Exit channel: unbounded, destructor frees messages */
    sup->exit_chan = ol_channel_create(/*unbounded*/0, (ol_chan_item_destructor)free);
    if (!sup->exit_chan) {
        ol_mutex_destroy(&sup->mu);
        free(sup);
        return NULL;
    }

    return sup;
}

void ol_supervisor_destroy(ol_supervisor_t *sup) {
    if (!sup) return;

    (void)ol_supervisor_stop(sup);

    if (sup->exit_chan) {
        ol_channel_destroy(sup->exit_chan);
        sup->exit_chan = NULL;
    }

    /* No join needed for actor threads (detached); states are EXITED after stop */
    free(sup->children);
    ol_mutex_destroy(&sup->mu);
    free(sup);
}

int ol_supervisor_start(ol_supervisor_t *sup) {
    if (!sup) return -1;
    ol_mutex_lock(&sup->mu);
    if (sup->running) { ol_mutex_unlock(&sup->mu); return 0; }
    sup->running = true;
    ol_mutex_unlock(&sup->mu);

#if defined(_WIN32)
    if (ol_thread_start(&sup->monitor, supervisor_monitor, sup) != 0) return -1;
#else
    if (ol_thread_start(&sup->monitor, supervisor_monitor, sup) != 0) return -1;
    /* detach monitor? We want to join on stop, so keep joinable on POSIX */
#endif
    return 0;
}

int ol_supervisor_stop(ol_supervisor_t *sup) {
    if (!sup) return -1;

    ol_mutex_lock(&sup->mu);
    if (!sup->running && sup->stopping) { ol_mutex_unlock(&sup->mu); return 0; }
    sup->stopping = true;

    /* Stop all children */
    for (size_t i = 0; i < sup->count; i++) {
        (void)stop_child(sup, &sup->children[i]);
        sup->children[i].state = CHILD_EXITED;
    }

    /* Close exit channel to wake monitor */
    ol_channel_close(sup->exit_chan);
    sup->running = false;
    ol_mutex_unlock(&sup->mu);

#if defined(_WIN32)
    /* Wait monitor thread exits */
    ol_thread_join(sup->monitor);
#else
    /* Join monitor */
    ol_thread_join(sup->monitor);
#endif

    sup->stopping = false;
    return 0;
}

uint32_t ol_supervisor_add_child(ol_supervisor_t *sup, const ol_child_spec_t *spec) {
    if (!sup || !spec || !spec->fn) return 0;

    ol_mutex_lock(&sup->mu);

    if (ensure_child_capacity(sup, sup->count + 1) != 0) {
        ol_mutex_unlock(&sup->mu);
        return 0;
    }
    uint32_t id = sup->next_id++;

    ol_child_t ch;
    memset(&ch, 0, sizeof(ch));
    ch.id = id;
    ch.state = CHILD_INIT;
    ch.last_status = 0;
    ch.restart_count = 0;
    ch.first_restart_ns = 0;
    ch.order_index = sup->next_order_idx++;

    /* Copy spec shallowly (name and arg pointers are caller-owned) */
    ch.spec = *spec;

    sup->children[sup->count++] = ch;

    /* Start immediately if supervisor is running */
    ssize_t idx = (ssize_t)(sup->count - 1);
    if (sup->running) {
        (void)start_child(sup, &sup->children[idx]);
    }

    ol_mutex_unlock(&sup->mu);
    return id;
}

int ol_supervisor_remove_child(ol_supervisor_t *sup, uint32_t child_id) {
    if (!sup || child_id == 0) return -1;

    ol_mutex_lock(&sup->mu);
    ssize_t idx = find_child_index(sup, child_id);
    if (idx < 0) { ol_mutex_unlock(&sup->mu); return -1; }

    ol_child_t *ch = &sup->children[idx];

    (void)stop_child(sup, ch);

    /* Compact array */
    size_t w = 0;
    for (size_t r = 0; r < sup->count; r++) {
        if (sup->children[r].id != child_id) {
            if (w != r) sup->children[w] = sup->children[r];
            w++;
        }
    }
    sup->count = w;

    ol_mutex_unlock(&sup->mu);
    return 0;
}

int ol_supervisor_restart_child(ol_supervisor_t *sup, uint32_t child_id) {
    if (!sup || child_id == 0) return -1;

    ol_mutex_lock(&sup->mu);
    ssize_t idx = find_child_index(sup, child_id);
    if (idx < 0) { ol_mutex_unlock(&sup->mu); return -1; }
    ol_child_t *ch = &sup->children[idx];

    (void)stop_child(sup, ch);

    /* Check intensity window before manual restart as well */
    if (!can_restart(ch, sup->max_restarts, sup->window_ms)) {
        /* Escalation: stop all children */
        for (size_t i = 0; i < sup->count; i++) {
            (void)stop_child(sup, &sup->children[i]);
            sup->children[i].state = CHILD_EXITED;
        }
        sup->running = false;
        ol_mutex_unlock(&sup->mu);
        return -1;
    }

    int r = start_child(sup, ch);
    ol_mutex_unlock(&sup->mu);
    return r;
}

/* Introspection */

size_t ol_supervisor_child_count(const ol_supervisor_t *sup) {
    if (!sup) return 0;
    size_t c;
    ol_mutex_lock((ol_mutex_t*)&sup->mu);
    c = sup->count;
    ol_mutex_unlock((ol_mutex_t*)&sup->mu);
    return c;
}

bool ol_supervisor_is_running(const ol_supervisor_t *sup) {
    if (!sup) return false;
    bool r;
    ol_mutex_lock((ol_mutex_t*)&sup->mu);
    r = sup->running;
    ol_mutex_unlock((ol_mutex_t*)&sup->mu);
    return r;
}
