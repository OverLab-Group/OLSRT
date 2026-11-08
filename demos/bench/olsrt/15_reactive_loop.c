/* OLSRT side: event loop + timer feeding subject. Native. */
#include "bench_util.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"
#include "ol_reactive.h"
#include <stdio.h>
#include <stdlib.h>

#define N 10
#define PERIOD_MS 50
static int g_received = 0;
static int g_next = 1;

static void on_next(void *item, void *ud) {
    (void)ud;
    /* v2: subject owns item (dtor=free); do NOT free here. */
    if (item) { (void)*(int *)item; }
    g_received++;
}
static void on_complete(void *ud) {
    ol_event_loop_t *loop = ud;
    ol_event_loop_stop(loop);
}
static void tick(ol_event_loop_t *loop, ol_ev_type_t t, int fd, void *ud) {
    (void)t; (void)fd;
    ol_subject_t *s = ud;
    int *v = malloc(sizeof(int));
    *v = g_next++;
    ol_subject_on_next(s, v);
    if (g_received >= N) {
        ol_subject_on_complete(s);
        ol_event_loop_stop(loop);
    }
}

int main(void) {
    printf("olsrt/15_reactive_loop: native (event loop + subject)\n");
    printf("%d ticks @ %dms\n\n", N, PERIOD_MS);
    uint64_t t0 = bench_now_ns();
    ol_event_loop_t *loop = ol_event_loop_create();
    ol_subject_t *s = ol_subject_create(loop, free);
    ol_observable_t *obs = ol_subject_as_observable(s);
    ol_rx_subscription_t *sub = ol_observable_subscribe(
        obs, on_next, NULL, on_complete, N, loop);
    ol_event_loop_register_timer(loop,
        ol_deadline_from_ms(PERIOD_MS), PERIOD_MS * 1000000LL, tick, s);
    ol_event_loop_run(loop);
    ol_rx_subscription_destroy(sub);
    ol_subject_destroy(s);
    ol_event_loop_destroy(loop);
    printf("ticks_received:     %d (expect %d)\n", g_received, N);
    bench_footer(t0, N, "ticks");
    return 0;
}
