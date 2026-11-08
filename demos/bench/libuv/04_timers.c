/* libuv side: uv_timer. Native. */
#include "bench_util.h"
#include <stdio.h>
#include <uv.h>

static uv_loop_t *g_loop;
static uv_timer_t t_os, t_per, t_stop;
static int oneshot_fires = 0, periodic_fires = 0;
static uint64_t oneshot_time = 0, periodic_times[64];
static int periodic_n = 0;

static void cb_os(uv_timer_t *t) {
    (void)t;
    oneshot_fires++;
    oneshot_time = bench_now_ns();
    /* v2: do NOT close here. cb_stop closes all three. */
}
static void cb_per(uv_timer_t *t) {
    (void)t;
    if (periodic_n < 64) periodic_times[periodic_n++] = bench_now_ns();
    periodic_fires++;
}
static void cb_stop(uv_timer_t *t) {
    uv_stop(g_loop);
    uv_close((uv_handle_t *)&t_os, NULL);
    uv_close((uv_handle_t *)&t_per, NULL);
    uv_close((uv_handle_t *)t, NULL);
}

int main(void) {
    printf("libuv/04_timers: native (uv_timer)\n");
    printf("one-shot@20ms, periodic@60ms, stop@400ms\n\n");
    uint64_t t0 = bench_now_ns();
    g_loop = uv_default_loop();
    uv_timer_init(g_loop, &t_os); uv_timer_init(g_loop, &t_per); uv_timer_init(g_loop, &t_stop);
    uv_timer_start(&t_os, cb_os, 20, 0);
    uv_timer_start(&t_per, cb_per, 60, 60);
    uv_timer_start(&t_stop, cb_stop, 400, 0);
    uv_run(g_loop, UV_RUN_DEFAULT);
    uint64_t sum_int = 0;
    for (int i = 1; i < periodic_n; i++)
        sum_int += periodic_times[i] - periodic_times[i - 1];
    double mean_int = periodic_n > 1 ? (double)sum_int / (periodic_n - 1) / 1e6 : 0.0;
    printf("oneshot_fires:      %d (expected 1)\n", oneshot_fires);
    printf("periodic_fires:     %d\n", periodic_fires);
    printf("mean_interval_ms:   %.2f (target 60)\n", mean_int);
    printf("oneshot_latency_ms: %.2f (target 20)\n",
           oneshot_time ? (double)(oneshot_time - t0) / 1e6 : 0.0);
    uv_loop_close(g_loop);
    bench_footer(t0, oneshot_fires + periodic_fires, "timer_fires");
    return 0;
}
