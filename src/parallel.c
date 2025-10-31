/* OLSRT - Parallel thread pool */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>

/* Work node */
typedef struct ol_work_node_s {
    ol_work_fn              fn;
    void                   *arg;
    struct ol_work_node_s  *next;
} ol_work_node_t;

/* Pool object */
struct ol_parallel_pool_s {
    int              threads;
    int              stopping;
    int              affinity;  /* 0 none, 1 compact, 2 scatter (placeholder) */
    pthread_t       *th;
    ol_work_node_t  *head;
    ol_work_node_t  *tail;
    pthread_mutex_t  mu;
    pthread_cond_t   cv;
};

/* Worker thread entry */
static unsigned __stdcall ol_worker_entry(void *p_) {
    ol_parallel_pool_t *p = (ol_parallel_pool_t*)p_;
    for (;;) {
        pthread_mutex_lock(&p->mu);
        while (!p->head && !p->stopping) {
            pthread_cond_wait(&p->cv, &p->mu);
        }
        if (p->stopping) {
            pthread_mutex_unlock(&p->mu);
            break;
        }
        ol_work_node_t *n = p->head;
        if (n) {
            p->head = n->next;
            if (!p->head) p->tail = NULL;
        }
        pthread_mutex_unlock(&p->mu);

        if (n) {
            uint64_t st_ms = 0;
            if (G_TRACE) st_ms = ol_monotonic_ms();
            n->fn(n->arg);
            if (G_TRACE) ol_trace_emit("parallel", "job", ol_monotonic_ms() - st_ms, OL_OK);
            free(n);
        }
    }
    return 0;
}

/* Create thread pool */
ol_parallel_pool_t* ol_parallel_pool_create(const ol_parallel_opts_t *opts) {
    ol_parallel_pool_t *p = (ol_parallel_pool_t*)calloc(1, sizeof(*p));
    if (!p) return NULL;

    p->threads  = (opts && opts->threads > 0) ? opts->threads : 4;
    p->affinity = (opts) ? opts->affinity : 0;

    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->cv, NULL);

    p->th = (pthread_t*)calloc((size_t)p->threads, sizeof(pthread_t));
    if (!p->th) { free(p); return NULL; }

    for (int i = 0; i < p->threads; i++) {
        if (pthread_create(&p->th[i], NULL, (pthread_start_routine)ol_worker_entry, p) != 0) {
            /* Best-effort: mark stopping and cleanup */
            p->stopping = 1;
            for (int j = 0; j < i; j++) pthread_join(p->th[j], NULL);
            free(p->th);
            pthread_mutex_destroy(&p->mu);
            pthread_cond_destroy(&p->cv);
            free(p);
            return NULL;
        }
    }
    return p;
}

/* Submit work to the pool */
int ol_parallel_submit(ol_parallel_pool_t *p, ol_work_fn fn, void *arg) {
    if (!p || !fn) return OL_ERR_STATE;
    ol_work_node_t *n = (ol_work_node_t*)malloc(sizeof(*n));
    if (!n) return OL_ERR_ALLOC;
    n->fn = fn; n->arg = arg; n->next = NULL;

    pthread_mutex_lock(&p->mu);
    if (!p->tail) {
        p->head = p->tail = n;
    } else {
        p->tail->next = n;
        p->tail       = n;
    }
    pthread_cond_signal(&p->cv);
    pthread_mutex_unlock(&p->mu);
    return OL_OK;
}

/* Shutdown pool: join threads and free pending work */
int ol_parallel_shutdown(ol_parallel_pool_t *p) {
    if (!p) return OL_ERR_STATE;

    pthread_mutex_lock(&p->mu);
    p->stopping = 1;
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->mu);

    for (int i = 0; i < p->threads; i++) {
        pthread_join(p->th[i], NULL);
    }
    free(p->th);

    /* Free any remaining work items */
    ol_work_node_t *n = p->head;
    while (n) {
        ol_work_node_t *next = n->next;
        free(n);
        n = next;
    }

    pthread_mutex_destroy(&p->mu);
    pthread_cond_destroy(&p->cv);
    free(p);
    return OL_OK;
}
