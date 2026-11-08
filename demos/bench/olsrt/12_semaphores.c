/* OLSRT side: 8 threads, cap 2. Native (ol_sem_t). */
#include "bench_util.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_lock_mutex.h"
#include "ol_parallel.h"
#include "ol_semaphores.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NW 8
#define MAXC 2
#define WMS 100

typedef struct { ol_sem_t s; ol_mutex_t m; int active, peak, done, wait_ms; } ctx_t;

static void worker(void *arg) {
    ctx_t *c = arg;
    uint64_t t0 = bench_now_ns();
    ol_sem_wait_until(&c->s, ol_deadline_from_ms(5000).when_ns);
    int wait = (int)((bench_now_ns() - t0) / 1000000);
    ol_mutex_lock(&c->m);
    c->active++;
    if (c->active > c->peak) c->peak = c->active;
    c->wait_ms += wait;
    ol_mutex_unlock(&c->m);

    struct timespec ts = { 0, WMS * 1000000L };
    nanosleep(&ts, NULL);

    ol_mutex_lock(&c->m);
    c->active--;
    c->done++;
    ol_mutex_unlock(&c->m);
    ol_sem_post(&c->s);
}

int main(void) {
    printf("olsrt/12_semaphores: native (ol_sem_t)\n");
    printf("%d threads, max_concurrent=%d, work=%dms\n\n", NW, MAXC, WMS);
    uint64_t t0 = bench_now_ns();
    ctx_t c = { 0 };
    ol_sem_init(&c.s, MAXC, MAXC);
    ol_mutex_init(&c.m);
    ol_parallel_pool_t *pool = ol_parallel_create(NW);
    for (int i = 0; i < NW; i++) ol_parallel_submit(pool, worker, &c);
    ol_parallel_flush(pool);
    ol_parallel_destroy(pool);
    printf("threads_total:      %d\n", NW);
    printf("threads_done:       %d\n", c.done);
    printf("peak_concurrent:    %d (limit %d)\n", c.peak, MAXC);
    printf("mean_wait_ms:       %.1f\n", c.done ? (double)c.wait_ms / c.done : 0);
    ol_sem_destroy(&c.s);
    ol_mutex_destroy(&c.m);
    bench_footer(t0, NW, "semaphore_ops");
    return 0;
}
