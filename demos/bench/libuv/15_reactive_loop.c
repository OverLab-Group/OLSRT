/* libuv side: uv_timer + callback. Built (~30 LoC). */
#include "bench_util.h"
#include <stdio.h>
#include <uv.h>

#define N 10
#define PERIOD_MS 50
static uv_loop_t *g_loop;
static uv_timer_t g_t;
static int g_received = 0, g_next = 1;

static void on_tick(uv_timer_t *t) {
    (void)t;
    (void)g_next++;
    g_received++;
    if (g_received >= N) {
        uv_close((uv_handle_t *)&g_t, NULL);
        uv_stop(g_loop);
    }
}

int main(void) {
    printf("libuv/15_reactive_loop: built (uv_timer + callback)\n");
    printf("%d ticks @ %dms\n\n", N, PERIOD_MS);
    uint64_t t0 = bench_now_ns();
    g_loop = uv_default_loop();
    uv_timer_init(g_loop, &g_t);
    uv_timer_start(&g_t, on_tick, PERIOD_MS, PERIOD_MS);
    uv_run(g_loop, UV_RUN_DEFAULT);
    uv_loop_close(g_loop);
    printf("ticks_received:     %d (expect %d)\n", g_received, N);
    bench_footer(t0, N, "ticks");
    return 0;
}
