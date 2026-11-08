/**
 * @file bench_channel.h
 * @brief Thread-safe FIFO with bounded capacity, used by the libuv
 *        mirrors that need a channel.
 *
 * libuv does not have a channel primitive. Every channel in the
 * libuv mirror suite is implemented on top of pthread mutex+condvar
 * and a linked list. This header is the single implementation, so
 * the demo files stay short.
 *
 * The API mirrors the OLSRT channel where possible:
 *   bc_init    <-> ol_channel_create
 *   bc_send    <-> ol_channel_send
 *   bc_recv    <-> ol_channel_recv
 *   bc_close   <-> ol_channel_close
 *   bc_destroy <-> ol_channel_destroy
 */

#ifndef BENCH_CHANNEL_H
#define BENCH_CHANNEL_H

#include <pthread.h>
#include <stdlib.h>

typedef struct ch_node {
    void *data;
    struct ch_node *next;
} ch_node_t;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    ch_node_t *head;
    ch_node_t *tail;
    int size;
    int cap;      /* 0 = unbounded */
    int closed;
} bench_channel_t;

static inline void bc_init(bench_channel_t *c, int cap)
{
    pthread_mutex_init(&c->mu, NULL);
    pthread_cond_init(&c->not_empty, NULL);
    pthread_cond_init(&c->not_full, NULL);
    c->head = c->tail = NULL;
    c->size = 0;
    c->cap = cap;
    c->closed = 0;
}

static inline void bc_send(bench_channel_t *c, void *d)
{
    pthread_mutex_lock(&c->mu);
    while (c->cap > 0 && c->size >= c->cap && !c->closed) {
        pthread_cond_wait(&c->not_full, &c->mu);
    }
    if (c->closed) {
        pthread_mutex_unlock(&c->mu);
        free(d);
        return;
    }
    ch_node_t *n = malloc(sizeof(*n));
    n->data = d;
    n->next = NULL;
    if (c->tail) c->tail->next = n;
    else         c->head = n;
    c->tail = n;
    c->size++;
    pthread_cond_signal(&c->not_empty);
    pthread_mutex_unlock(&c->mu);
}

static inline void *bc_recv(bench_channel_t *c)
{
    pthread_mutex_lock(&c->mu);
    while (c->size == 0 && !c->closed) {
        pthread_cond_wait(&c->not_empty, &c->mu);
    }
    if (c->size == 0) {
        pthread_mutex_unlock(&c->mu);
        return NULL;
    }
    ch_node_t *n = c->head;
    c->head = n->next;
    if (!c->head) c->tail = NULL;
    c->size--;
    void *d = n->data;
    free(n);
    pthread_cond_signal(&c->not_full);
    pthread_mutex_unlock(&c->mu);
    return d;
}

static inline void bc_close(bench_channel_t *c)
{
    pthread_mutex_lock(&c->mu);
    c->closed = 1;
    pthread_cond_broadcast(&c->not_empty);
    pthread_cond_broadcast(&c->not_full);
    pthread_mutex_unlock(&c->mu);
}

static inline void bc_destroy(bench_channel_t *c)
{
    pthread_mutex_destroy(&c->mu);
    pthread_cond_destroy(&c->not_empty);
    pthread_cond_destroy(&c->not_full);
}

#endif /* BENCH_CHANNEL_H */
