/**
 * @file 06_dataflow.c
 * @brief Dataflow graph demo: source -> doubler -> sink.
 *
 * Builds a three-node chain and pushes items into the source node.
 * Worker threads pull items from node inboxes and dispatch handlers.
 *
 * Note: the current worker loop uses a busy poll (no wake event yet);
 * the demo keeps its wall-clock time small to avoid spinning.
 */

#include "ol_common.h"
#include "ol_dataflow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    const char* name;
} node_ctx_t;

static int source_handler(void* ctx, void* item,
                          int (*emit)(void*, int, void*),
                          void* emit_ctx) {
    node_ctx_t* n = (node_ctx_t*)ctx;
    int v = *(int*)item;
    printf("  [%-8s] received %d, forwarding\n", n->name, v);
    emit(emit_ctx, 0, item);
    return 0;
}

static int doubler_handler(void* ctx, void* item,
                           int (*emit)(void*, int, void*),
                           void* emit_ctx) {
    node_ctx_t* n = (node_ctx_t*)ctx;
    int v = *(int*)item;
    free(item);

    int* out = (int*)malloc(sizeof(int));
    if (!out) return -1;
    *out = v * 2;
    printf("  [%-8s] received %d, emitting %d\n", n->name, v, *out);
    emit(emit_ctx, 0, out);
    return 0;
}

static int sink_handler(void* ctx, void* item,
                        int (*emit)(void*, int, void*),
                        void* emit_ctx) {
    (void)emit; (void)emit_ctx;
    node_ctx_t* n = (node_ctx_t*)ctx;
    int v = *(int*)item;
    free(item);
    printf("  [%-8s] final value = %d\n", n->name, v);
    return 0;
}

int main(void) {
    printf("OLSRT Demo 06: Dataflow Graph\n");
    printf("==============================\n\n");

    ol_df_graph_t* g = ol_df_graph_create(2);
    if (!g) { fprintf(stderr, "graph create failed\n"); return 1; }

    node_ctx_t src_ctx  = { "source"  };
    node_ctx_t dbl_ctx  = { "doubler" };
    node_ctx_t sink_ctx = { "sink"    };

    ol_df_node_t* n1 = ol_df_node_create(g, source_handler,  &src_ctx,  1);
    ol_df_node_t* n2 = ol_df_node_create(g, doubler_handler, &dbl_ctx,  1);
    ol_df_node_t* n3 = ol_df_node_create(g, sink_handler,    &sink_ctx, 0);

    if (!n1 || !n2 || !n3) {
        fprintf(stderr, "node create failed\n");
        ol_df_graph_destroy(g);
        return 1;
    }

    ol_df_connect(g, n1, 0, n2, 32, free);
    ol_df_connect(g, n2, 0, n3, 32, free);

    printf("graph built: %zu nodes, %zu edges\n",
           ol_df_graph_node_count(g), ol_df_graph_edge_count(g));

    ol_df_graph_start(g);

    printf("\npushing 5 items (10, 20, 30, 40, 50):\n");
    for (int i = 1; i <= 5; i++) {
        int* v = (int*)malloc(sizeof(int));
        *v = i * 10;
        ol_df_push(g, n1, v);
    }

    struct timespec ts = { 0, 150 * 1000 * 1000 };   /* 150 ms */
    nanosleep(&ts, NULL);

    ol_df_graph_stop(g);
    ol_df_graph_destroy(g);

    printf("\n[OK] dataflow demo completed\n");
    return 0;
}
