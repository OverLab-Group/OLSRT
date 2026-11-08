/* OLSRT side: 1000 items through source→doubler→sink. NO sleep. */
#include "bench_util.h"
#include "ol_dataflow.h"
#include <stdio.h>
#include <stdlib.h>

#define N 1000
static int g_sink_count = 0, g_sink_last = 0;

static int sink_h(void *ctx, void *item, int (*emit)(void *, int, void *), void *e) {
    (void)ctx; (void)emit; (void)e;
    if (item) { g_sink_count++; g_sink_last = *(int *)item; free(item); }
    return 0;
}
static int dbl_h(void *ctx, void *item, int (*emit)(void *, int, void *), void *e) {
    (void)ctx;
    int v = *(int *)item; free(item);
    int *o = malloc(sizeof(int)); *o = v * 2;
    emit(e, 0, o);
    return 0;
}
static int src_h(void *ctx, void *item, int (*emit)(void *, int, void *), void *e) {
    (void)ctx;
    emit(e, 0, item);
    return 0;
}

int main(void) {
    printf("olsrt/06_dataflow: native (ol_dataflow)\n");
    printf("%d items through source->doubler->sink\n\n", N);
    uint64_t t0 = bench_now_ns();
    ol_df_graph_t *g = ol_df_graph_create(2);
    ol_df_node_t *a = ol_df_node_create(g, src_h,  NULL, 1);
    ol_df_node_t *b = ol_df_node_create(g, dbl_h,  NULL, 1);
    ol_df_node_t *c = ol_df_node_create(g, sink_h, NULL, 0);
    ol_df_edge_t *e1 = ol_df_connect(g, a, 0, b, 64, free);
    ol_df_edge_t *e2 = ol_df_connect(g, b, 0, c, 64, free);
    ol_df_graph_start(g);
    for (int i = 1; i <= N; i++) {
        int *v = malloc(sizeof(int)); *v = i;
        ol_df_push(g, a, v);
    }
    while (g_sink_count < N) {
        struct timespec ts = { 0, 1000000 };
        nanosleep(&ts, NULL);
        if (bench_now_ns() - t0 > 30000000000ULL) break;
    }
    ol_df_graph_stop(g);
    ol_df_disconnect(g, e2);
    ol_df_disconnect(g, e1);
    ol_df_graph_destroy(g);
    printf("items_pushed:       %d\n", N);
    printf("items_sunk:         %d\n", g_sink_count);
    printf("last_value:         %d (expect %d)\n", g_sink_last, N * 2);
    bench_footer(t0, N, "items");
    return 0;
}
