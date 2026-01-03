#include "ol_parallel.h"
#include "ol_lock_mutex.h"

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
#else
  #include <pthread.h>
  typedef pthread_t ol_thread_t;
  static int  ol_thread_start(ol_thread_t *t, void *(*fn)(void*), void *arg) {
      return (pthread_create(t, NULL, fn, arg) == 0) ? 0 : -1;
  }
  static void ol_thread_join(ol_thread_t t) { (void)pthread_join(t, NULL); }
#endif

/* Queue node */
typedef struct ol_task_node {
    ol_task_fn fn;
    void *arg;
    struct ol_task_node *next;
} ol_task_node_t;

/* Thread pool */
struct ol_parallel_pool {
    /* Workers */
    ol_thread_t *threads;
    size_t       nthreads;

    /* Work queue (FIFO) */
    ol_task_node_t *q_head;
    ol_task_node_t *q_tail;
    size_t          q_size;

    /* Synchronization */
    ol_mutex_t  mu;
    ol_cond_t   cv_has_work;   /* workers wait for work */
    ol_cond_t   cv_idle;       /* flush waits for idle (no work, no active workers) */

    /* State flags */
    bool running;       /* accepting work */
    bool shutting_down; /* shutdown in progress */

    /* Active workers currently executing a task (for flush) */
    size_t active_workers;
};

/* Internal helpers */

static void ol_enqueue_task(ol_parallel_pool_t *p, ol_task_fn fn, void *arg) {
    ol_task_node_t *node = (ol_task_node_t*)malloc(sizeof(ol_task_node_t));
    node->fn = fn;
    node->arg = arg;
    node->next = NULL;
    if (!p->q_tail) {
        p->q_head = p->q_tail = node;
    } else {
        p->q_tail->next = node;
        p->q_tail = node;
    }
    p->q_size++;
}

static int ol_dequeue_task(ol_parallel_pool_t *p, ol_task_fn *out_fn, void **out_arg) {
    ol_task_node_t *node = p->q_head;
    if (!node) return 0;
    p->q_head = node->next;
    if (!p->q_head) p->q_tail = NULL;
    p->q_size--;
    *out_fn  = node->fn;
    *out_arg = node->arg;
    free(node);
    return 1;
}

static void ol_clear_queue(ol_parallel_pool_t *p) {
    while (p->q_head) {
        ol_task_node_t *n = p->q_head;
        p->q_head = n->next;
        free(n);
    }
    p->q_tail = NULL;
    p->q_size = 0;
}

/* Worker routines */
#if defined(_WIN32)
static DWORD WINAPI ol_worker_main(LPVOID param)
#else
static void* ol_worker_main(void *param)
#endif
{
    ol_parallel_pool_t *p = (ol_parallel_pool_t*)param;

    for (;;) {
        ol_mutex_lock(&p->mu);

        /* Wait for work or shutdown */
        while (p->running && p->q_size == 0) {
            /* If flush is waiting, signal idle when no active workers and no tasks */
            if (p->active_workers == 0) {
                ol_cond_broadcast(&p->cv_idle);
            }
            int r = ol_cond_wait_until(&p->cv_has_work, &p->mu, /*infinite*/ 0);
            (void)r; /* ignore spurious wakeups */
        }

        /* If not running and no work queued: exit thread */
        if (!p->running && p->q_size == 0) {
            ol_mutex_unlock(&p->mu);
            break;
        }

        /* Dequeue one task */
        ol_task_fn fn = NULL;
        void *arg = NULL;
        if (ol_dequeue_task(p, &fn, &arg)) {
            p->active_workers++;
            ol_mutex_unlock(&p->mu);

            /* Execute outside the lock */
            fn(arg);

            ol_mutex_lock(&p->mu);
            p->active_workers--;

            /* If queue empty and no active workers, signal idle for flush/shutdown */
            if (p->q_size == 0 && p->active_workers == 0) {
                ol_cond_broadcast(&p->cv_idle);
            }
            ol_mutex_unlock(&p->mu);
        } else {
            /* No task despite condition: loop back */
            ol_mutex_unlock(&p->mu);
        }
    }

#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

/* Public API */

ol_parallel_pool_t* ol_parallel_create(size_t num_threads) {
    if (num_threads == 0) num_threads = 1;

    ol_parallel_pool_t *p = (ol_parallel_pool_t*)calloc(1, sizeof(ol_parallel_pool_t));
    if (!p) return NULL;

    p->threads = (ol_thread_t*)calloc(num_threads, sizeof(ol_thread_t));
    if (!p->threads) { free(p); return NULL; }

    p->nthreads = num_threads;
    p->q_head = p->q_tail = NULL;
    p->q_size = 0;
    p->running = true;
    p->shutting_down = false;
    p->active_workers = 0;

    if (ol_mutex_init(&p->mu) != 0) { free(p->threads); free(p); return NULL; }
    if (ol_cond_init(&p->cv_has_work) != 0) { ol_mutex_destroy(&p->mu); free(p->threads); free(p); return NULL; }
    if (ol_cond_init(&p->cv_idle) != 0) {
        ol_cond_destroy(&p->cv_has_work);
        ol_mutex_destroy(&p->mu);
        free(p->threads);
        free(p);
        return NULL;
    }

    /* Start workers */
    for (size_t i = 0; i < num_threads; i++) {
#if defined(_WIN32)
        if (ol_thread_start(&p->threads[i], ol_worker_main, p) != 0)
#else
        if (ol_thread_start(&p->threads[i], ol_worker_main, p) != 0)
#endif
        {
            /* Rollback on failure: request shutdown and join started workers */
            ol_mutex_lock(&p->mu);
            p->running = false;
            ol_cond_broadcast(&p->cv_has_work);
            ol_mutex_unlock(&p->mu);
            for (size_t j = 0; j < i; j++) {
                ol_thread_join(p->threads[j]);
            }
            ol_cond_destroy(&p->cv_idle);
            ol_cond_destroy(&p->cv_has_work);
            ol_mutex_destroy(&p->mu);
            free(p->threads);
            free(p);
            return NULL;
        }
    }

    return p;
}

/* Submit — upgraded to return -2 when pool not running to distinguish from generic error */
int ol_parallel_submit(ol_parallel_pool_t *p, ol_task_fn fn, void *arg) {
    if (!p || !fn) return -1;
    ol_mutex_lock(&p->mu);
    if (!p->running || p->shutting_down) {
        ol_mutex_unlock(&p->mu);
        return -2; /* not accepting work */
    }
    ol_enqueue_task(p, fn, arg);
    /* Wake one worker */
    ol_cond_signal(&p->cv_has_work);
    ol_mutex_unlock(&p->mu);
    return 0;
}

int ol_parallel_flush(ol_parallel_pool_t *p) {
    if (!p) return -1;
    ol_mutex_lock(&p->mu);
    /* Wait until queue empty and no active workers */
    while (p->q_size != 0 || p->active_workers != 0) {
        int r = ol_cond_wait_until(&p->cv_idle, &p->mu, /*infinite*/ 0);
        if (r < 0) { ol_mutex_unlock(&p->mu); return -1; }
    }
    ol_mutex_unlock(&p->mu);
    return 0;
}

int ol_parallel_shutdown(ol_parallel_pool_t *p, bool drain) {
    if (!p) return -1;

    ol_mutex_lock(&p->mu);
    p->shutting_down = true;
    /* Stop accepting new tasks */
    p->running = false;

    if (!drain) {
        /* Cancel pending tasks (not-yet-started) */
        ol_clear_queue(p);
    }
    /* Wake all workers so they can exit (queue may be empty or will be drained) */
    ol_cond_broadcast(&p->cv_has_work);
    ol_mutex_unlock(&p->mu);

    /* If drain requested, wait until workers finish outstanding tasks */
    if (drain) (void)ol_parallel_flush(p);

    /* Join all worker threads */
    for (size_t i = 0; i < p->nthreads; i++) {
        ol_thread_join(p->threads[i]);
    }

    return 0;
}

void ol_parallel_destroy(ol_parallel_pool_t *p) {
    if (!p) return;
    /* Ensure shutdown (drain) */
    (void)ol_parallel_shutdown(p, true);

    /* Cleanup synchronization and storage */
    ol_cond_destroy(&p->cv_idle);
    ol_cond_destroy(&p->cv_has_work);
    ol_mutex_destroy(&p->mu);

    /* Clear any remaining queue nodes defensively */
    ol_clear_queue(p);

    free(p->threads);
    free(p);
}

/* Introspection */

size_t ol_parallel_thread_count(const ol_parallel_pool_t *p) {
    return p ? p->nthreads : 0;
}

size_t ol_parallel_queue_size(const ol_parallel_pool_t *p) {
    if (!p) return 0;
    size_t sz;
    ol_mutex_lock((ol_mutex_t*)&p->mu);
    sz = p->q_size;
    ol_mutex_unlock((ol_mutex_t*)&p->mu);
    return sz;
}

bool ol_parallel_is_running(const ol_parallel_pool_t *p) {
    if (!p) return false;
    bool r;
    ol_mutex_lock((ol_mutex_t*)&p->mu);
    r = p->running;
    ol_mutex_unlock((ol_mutex_t*)&p->mu);
    return r;
}
