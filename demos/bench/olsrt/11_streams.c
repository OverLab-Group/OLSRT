/* OLSRT side: 1000 items with demand=1 backpressure. Native. */
#include "bench_util.h"
#include "ol_common.h"
#include "ol_event_loop.h"
#include "ol_streams.h"
#include <stdio.h>
#include <stdlib.h>

#define N 1000
static int g_received = 0;
static ol_subscription_t *g_sub = NULL;

static void on_next(void *item, void *ud) {
    (void)ud;
    if (item) {
        int v = *(int*)item;
        volatile long long work = 0;
        for (int _i = 0; _i < 100; _i++) work += (long long)v * _i;
        (void)work;
        g_received++;
    }
    if (g_received < N && g_sub)
        (void)ol_subscription_request(g_sub, 1);
}

int main(void) {
    printf("olsrt/11_streams: native (ol_stream_t, demand=1)\n");
    printf("%d items with demand=1 backpressure\n\n", N);
    uint64_t t0 = bench_now_ns();
    ol_event_loop_t *loop = ol_event_loop_create();
    ol_stream_t *src = ol_stream_create(loop, free);
    g_sub = ol_stream_subscribe(src, on_next, NULL, NULL, 1, NULL);
    for (int i = 1; i <= N; i++) {
        int *v = malloc(sizeof(int));
        *v = i;
        ol_stream_emit_next(src, v);
    }
    ol_stream_emit_complete(src);
    ol_subscription_destroy(g_sub);
    ol_stream_destroy(src);
    ol_event_loop_destroy(loop);
    printf("items_emitted:      %d\n", N);
    printf("items_received:     %d\n", g_received);
    bench_footer(t0, N, "items");
    return 0;
}
