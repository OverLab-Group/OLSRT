/* OLSRT side: 1000 synchronous emissions. Native (ol_subject_t). */
#include "bench_util.h"
#include "ol_common.h"
#include "ol_event_loop.h"
#include "ol_reactive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 1000
static int g_received = 0;

static void on_next(void *item, void *ud) {
    (void)ud;
    if (item) {
        int v = *(int*)item;
        volatile long long work = 0;
        for (int _i = 0; _i < 100; _i++) work += (long long)v * _i;
        (void)work;
        g_received++;
    }
}

int main(void) {
    printf("olsrt/10_reactive: native (ol_subject_t)\n");
    printf("%d synchronous emissions\n\n", N);
    uint64_t t0 = bench_now_ns();
    ol_event_loop_t *loop = ol_event_loop_create();
    ol_subject_t *s = ol_subject_create(loop, free);
    ol_observable_t *obs = ol_subject_as_observable(s);
    ol_rx_subscription_t *sub = ol_observable_subscribe(
        obs, on_next, NULL, NULL, N, NULL);
    for (int i = 1; i <= N; i++) {
        int *v = malloc(sizeof(int));
        *v = i;
        ol_subject_on_next(s, v);
    }
    ol_subject_on_complete(s);
    ol_rx_subscription_destroy(sub);
    ol_subject_destroy(s);
    ol_event_loop_destroy(loop);
    printf("items_emitted:      %d\n", N);
    printf("items_received:     %d\n", g_received);
    bench_footer(t0, N, "items");
    return 0;
}
