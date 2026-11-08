/* libuv side: uv_queue_work with default pool. Native. */
#include "bench_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <uv.h>

#define N 100
#define ITERS 200000

static uv_loop_t *g_loop;
static uv_mutex_t g_mu;
static int g_done = 0;
static volatile long long g_sink;

typedef struct { uv_work_t w; int id; } task_t;

static void work_cb(uv_work_t *w) {
    (void)w;
    long long acc = 0;
    for (long long i = 0; i < ITERS; i++) acc += i * i;
    g_sink += acc;
}
static void after_cb(uv_work_t *w, int status) {
    (void)w; (void)status;
    uv_mutex_lock(&g_mu);
    g_done++;
    int d = g_done;
    uv_mutex_unlock(&g_mu);
    if (d == N) uv_stop(g_loop);
}

int main(void) {
    printf("libuv/03_parallel: native (uv_queue_work)\n");
    printf("%d tasks, %d iters each, default pool\n\n", N, ITERS);
    uint64_t t0 = bench_now_ns();
    g_loop = uv_default_loop();
    uv_mutex_init(&g_mu);
    task_t *tasks = calloc(N, sizeof(*tasks));
    for (int i = 0; i < N; i++) {
        tasks[i].id = i;
        tasks[i].w.data = &tasks[i];
        uv_queue_work(g_loop, &tasks[i].w, work_cb, after_cb);
    }
    uv_run(g_loop, UV_RUN_DEFAULT);
    printf("tasks_submitted:    %d\n", N);
    printf("tasks_completed:    %d\n", g_done);
    printf("workers:            4\n");
    printf("iters_per_task:     %d\n", ITERS);
    uv_mutex_destroy(&g_mu);
    uv_loop_close(g_loop);
    free(tasks);
    bench_footer(t0, N, "tasks");
    return 0;
}
