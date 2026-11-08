/* OLSRT side: 100 CPU tasks on 4-worker pool. Built (pthread pool). */
#include "bench_util.h"
#include "ol_parallel.h"
#include <stdio.h>
#include <stdlib.h>

#define N 100
#define ITERS 200000

static volatile long long g_sink;

static void task(void *arg) {
    (void)arg;
    long long acc = 0;
    for (long long i = 0; i < ITERS; i++) acc += i * i;
    g_sink += acc;
}

int main(void) {
    printf("olsrt/03_parallel: built (thread pool on pthreads)\n");
    printf("%d tasks, %d iters each, 4 workers\n\n", N, ITERS);
    uint64_t t0 = bench_now_ns();
    ol_parallel_pool_t *pool = ol_parallel_create(4);
    for (int i = 0; i < N; i++)
        ol_parallel_submit(pool, task, NULL);
    ol_parallel_flush(pool);
    ol_parallel_destroy(pool);
    printf("tasks_submitted:    %d\n", N);
    printf("tasks_completed:    %d\n", N);
    printf("workers:            4\n");
    printf("iters_per_task:     %d\n", ITERS);
    bench_footer(t0, N, "tasks");
    return 0;
}
