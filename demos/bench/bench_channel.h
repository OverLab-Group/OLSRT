/**
 * @file bench_channel.h
 * @brief Bounded FIFO with mutex+condvar. Used only by the libuv
 * mirrors for demos that need a "channel" primitive, which libuv
 * does not ship. Counts as "built" (about 80 LoC) in BENCHMARK.md.
 */

#ifndef BENCH_CHANNEL_H
#define BENCH_CHANNEL_H

#include <pthread.h>
#include <stdlib.h>

typedef struct bench_ch_node {
    void *data;
    struct bench_ch_node *next;
} bench_ch_node_t;

typedef struct {
    pthread_mutex_t mu, cvmu;
    pthread_cond_t not_empty, not_full;
    bench_ch_node_t *head, *tail;
    int size, cap, closed;
} bench_channel_t;

static inline void bc_init(bench_channel_t *c, int cap) {
    pthread_mutex_init(&c->mu, NULL);
    pthread_cond_init(&c->not_empty, NULL);
    pthread_cond_init(&c->not_full, NULL);
    c->head = c->tail = NULL;
    c->size = 0;
    c->cap = cap;
    c->closed = 0;
}

static inline void bc_send(bench_channel_t *c, void *d) {
    pthread_mutex_lock(&c->mu);
    while (c->cap > 0 && c->size >= c->cap && !c->closed)
        pthread_cond_wait(&c->not_full, &c->mu);
    if (c->closed) { pthread_mutex_unlock(&c->mu); free(d); return; }
    bench_ch_node_t *n = malloc(sizeof(*n));
    n->data = d; n->next = NULL;
    if (c->tail) c->tail->next = n; else c->head = n;
    c->tail = n; c->size++;
    pthread_cond_signal(&c->not_empty);
    pthread_mutex_unlock(&c->mu);
}

static inline void *bc_recv(bench_channel_t *c) {
    pthread_mutex_lock(&c->mu);
    while (c->size == 0 && !c->closed)
        pthread_cond_wait(&c->not_empty, &c->mu);
    if (c->size == 0) { pthread_mutex_unlock(&c->mu); return NULL; }
    bench_ch_node_t *n = c->head;
    c->head = n->next;
    if (!c->head) c->tail = NULL;
    c->size--;
    void *d = n->data; free(n);
    pthread_cond_signal(&c->not_full);
    pthread_mutex_unlock(&c->mu);
    return d;
}

static inline void bc_close(bench_channel_t *c) {
    pthread_mutex_lock(&c->mu);
    c->closed = 1;
    pthread_cond_broadcast(&c->not_empty);
    pthread_cond_broadcast(&c->not_full);
    pthread_mutex_unlock(&c->mu);
}

static inline void bc_destroy(bench_channel_t *c) {
    pthread_mutex_destroy(&c->mu);
    pthread_cond_destroy(&c->not_empty);
    pthread_cond_destroy(&c->not_full);
}

#endif
