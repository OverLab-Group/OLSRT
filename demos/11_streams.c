/**
 * @file 11_streams.c
 * @brief Stream API with operators and backpressure.
 *
 * @details
 * A source stream emits the sequence 1..10. A filter keeps even
 * numbers; a take(3) closes the pipeline after three items. The
 * subscriber processes each item and requests the next one
 * explicitly (demand-driven backpressure).
 */

#include "ol_common.h"
#include "ol_event_loop.h"
#include "ol_streams.h"

#include <stdio.h>
#include <stdlib.h>

static int g_received = 0;

static void on_next(void *item, void *ud) {
    ol_subscription_t *sub = (ol_subscription_t*)ud;
    int v = *(int*)item;
    printf("[subscriber] item = %d\n", v);
    g_received++;
    free(item);
    /* Ask for the next item. */
    (void)ol_subscription_request(sub, 1);
}

static void on_complete(void *ud) {
    (void)ud;
    printf("[subscriber] complete\n");
}

static bool is_even(const void *item, void *ud) {
    (void)ud;
    return (*(const int*)item % 2) == 0;
}

int main(void) {
    printf("OLSRT Demo 11: Streams\n");
    printf("======================\n\n");

    ol_event_loop_t *loop = ol_event_loop_create();
    if (!loop) return 1;

    ol_stream_t *src = ol_stream_create(loop, free);
    ol_stream_t *filtered = ol_stream_filter(src, is_even, NULL);
    ol_stream_t *taken    = ol_stream_take(filtered, 3);

    ol_subscription_t *sub = ol_stream_subscribe(
        taken, on_next, NULL, on_complete,
        /* initial demand */ 1, NULL);
    if (!sub) {
        ol_stream_destroy(taken);
        ol_stream_destroy(filtered);
        ol_stream_destroy(src);
        ol_event_loop_destroy(loop);
        return 1;
    }

    printf("[main] emitting 1..10\n\n");
    for (int i = 1; i <= 10; i++) {
        int *v = malloc(sizeof(int));
        *v = i;
        ol_stream_emit_next(src, v);
    }
    ol_stream_emit_complete(src);

    printf("\n[main] received %d items\n", g_received);

    ol_subscription_destroy(sub);
    ol_stream_destroy(taken);
    ol_stream_destroy(filtered);
    ol_stream_destroy(src);
    ol_event_loop_destroy(loop);

    return g_received == 3 ? 0 : 2;
}
