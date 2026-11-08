/* OLSRT side: event loop timers. Native. */
#include "bench_util.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"
#include <stdio.h>

static ol_event_loop_t *g_loop;
static int oneshot_fires = 0, periodic_fires = 0;
static uint64_t oneshot_time = 0, periodic_times[64];
static int periodic_n = 0;

static void cb_os(ol_event_loop_t *l, ol_ev_type_t t, int fd, void *u) {
    (void)l; (void)t; (void)fd; (void)u;
    oneshot_fires++;
    oneshot_time = bench_now_ns();
}
static void cb_per(ol_event_loop_t *l, ol_ev_type_t t, int fd, void *u) {
    (void)l; (void)t; (void)fd; (void)u;
    if (periodic_n < 64) periodic_times[periodic_n++] = bench_now_ns();
    periodic_fires++;
}
static void cb_stop(ol_event_loop_t *l, ol_ev_type_t t, int fd, void *u) {
    (void)t; (void)fd; (void)u;
    ol_event_loop_stop(l);
}

int main(void) {
    printf("olsrt/04_timers: native (ol_event_loop timers)\n");
    printf("one-shot@20ms, periodic@60ms, stop@400ms\n\n");
    uint64_t t0 = bench_now_ns();
    g_loop = ol_event_loop_create();
    ol_event_loop_register_timer(g_loop, ol_deadline_from_ms(20), 0, cb_os, NULL);
    ol_event_loop_register_timer(g_loop, ol_deadline_from_ms(60), 60 * 1000000LL, cb_per, NULL);
    ol_event_loop_register_timer(g_loop, ol_deadline_from_ms(400), 0, cb_stop, NULL);
    ol_event_loop_run(g_loop);
    uint64_t sum_int = 0;
    for (int i = 1; i < periodic_n; i++)
        sum_int += periodic_times[i] - periodic_times[i - 1];
    double mean_int = periodic_n > 1 ? (double)sum_int / (periodic_n - 1) / 1e6 : 0.0;
    printf("oneshot_fires:      %d (expected 1)\n", oneshot_fires);
    printf("periodic_fires:     %d\n", periodic_fires);
    printf("mean_interval_ms:   %.2f (target 60)\n", mean_int);
    printf("oneshot_latency_ms: %.2f (target 20)\n",
           oneshot_time ? (double)(oneshot_time - t0) / 1e6 : 0.0);
    ol_event_loop_destroy(g_loop);
    bench_footer(t0, oneshot_fires + periodic_fires, "timer_fires");
    return 0;
}
