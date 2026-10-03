/**
 * @file 10_reactive.c
 * @brief Reactive subject with demand-based subscription.
 *
 * @details
 * A subject emits integers. A single subscriber receives each value
 * and prints it. After SEEN_TARGET values the demo reports success.
 *
 * Operator chaining is deliberately not used here. In v1.3.2 the
 * map / filter / take operators store a reference to their source
 * observable but never subscribe to it, so items emitted into the
 * source never reach the operator's callback. Wiring the subject
 * directly to the subscriber shows the flow that does work today.
 * Operator chaining is scheduled for v1.3.3.
 */

#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"
#include "ol_reactive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N_EMIT      8
#define SEEN_TARGET 5

static int g_seen = 0;

static void on_next(void *item, void *ud)
{
    (void)ud;
    int v = item ? *(int *)item : -1;
    g_seen++;
    printf("[subscriber] item %d = %d  (doubled = %d)\n",
           g_seen, v, v * 2);
}

static void on_complete(void *ud)
{
    (void)ud;
    printf("[subscriber] complete\n");
}

int main(void)
{
    printf("OLSRT Demo 10: Reactive\n");
    printf("=======================\n\n");

    ol_event_loop_t *loop = ol_event_loop_create();
    if (!loop) return 1;

    ol_subject_t *s = ol_subject_create(loop, free);
    if (!s) { ol_event_loop_destroy(loop); return 1; }

    ol_observable_t *obs = ol_subject_as_observable(s);

    ol_rx_subscription_t *sub = ol_observable_subscribe(
        obs, on_next, NULL, on_complete,
        N_EMIT,     /* demand: allow all of them through */
        NULL);
    if (!sub) {
        ol_subject_destroy(s);
        ol_event_loop_destroy(loop);
        return 1;
    }

    printf("[main] emitting 1..%d\n\n", N_EMIT);
    for (int i = 1; i <= N_EMIT && g_seen < SEEN_TARGET; i++) {
        int *v = malloc(sizeof(int));
        if (!v) break;
        *v = i;
        ol_subject_on_next(s, v);
    }

    ol_subject_on_complete(s);

    printf("\n[main] seen %d items\n", g_seen);

    ol_rx_subscription_destroy(sub);
    ol_subject_destroy(s);
    ol_event_loop_destroy(loop);

    if (g_seen >= SEEN_TARGET) {
        printf("[OK] subject delivered %d items\n", g_seen);
        return 0;
    }
    printf("[FAIL] expected at least %d items\n", SEEN_TARGET);
    return 2;
}
