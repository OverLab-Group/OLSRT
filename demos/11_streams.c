/**
 * @file 11_streams.c
 * @brief Stream API with backpressure via demand.
 *
 * A stream emits the sequence 1..N. A subscriber requests one item
 * at a time with ol_subscription_request, so the stream delivers
 * exactly as many items as the subscriber asked for.
 *
 * Ownership: the stream is created with dtor = free, which means
 * the stream owns the items it is given. The subscriber must NOT
 * free them. A previous version of this demo freed the item in
 * on_next and then the stream freed it again after delivery, which
 * aborted with "double free detected in tcache 2".
 */

#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"
#include "ol_streams.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N_EMIT       10
#define TAKE_TARGET  3

static int g_received = 0;
static ol_subscription_t *g_sub = NULL;

static void on_next(void *item, void *ud)
{
    (void)ud;
    int v = item ? *(int *)item : -1;
    g_received++;
    printf("[subscriber] item %d = %d\n", g_received, v);
    /* Do NOT free item here; the stream owns it and will release
     * it after this callback returns. */

    if (g_received < TAKE_TARGET && g_sub) {
        (void)ol_subscription_request(g_sub, 1);
    }
}

static void on_complete(void *ud)
{
    (void)ud;
    printf("[subscriber] complete\n");
}

int main(void)
{
    printf("OLSRT Demo 11: Streams\n");
    printf("======================\n\n");

    ol_event_loop_t *loop = ol_event_loop_create();
    if (!loop) return 1;

    ol_stream_t *src = ol_stream_create(loop, free);
    if (!src) { ol_event_loop_destroy(loop); return 1; }

    g_sub = ol_stream_subscribe(src, on_next, NULL, on_complete,
                                1, NULL);
    if (!g_sub) {
        ol_stream_destroy(src);
        ol_event_loop_destroy(loop);
        return 1;
    }

    printf("[main] emitting 1..%d\n\n", N_EMIT);
    for (int i = 1; i <= N_EMIT && g_received < TAKE_TARGET; i++) {
        int *v = malloc(sizeof(int));
        if (!v) break;
        *v = i;
        ol_stream_emit_next(src, v);
    }
    ol_stream_emit_complete(src);

    printf("\n[main] received %d items\n", g_received);

    ol_subscription_destroy(g_sub);
    ol_stream_destroy(src);
    ol_event_loop_destroy(loop);

    if (g_received >= TAKE_TARGET) {
        printf("[OK] received %d items\n", g_received);
        return 0;
    }
    printf("[FAIL] expected at least %d items\n", TAKE_TARGET);
    return 2;
}
