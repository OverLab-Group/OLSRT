/**
 * @file 10_reactive.c
 * @brief Reactive subject with map/filter/take operators.
 *
 * @details
 * A subject emits integers. A pipeline maps n to n * 2, filters
 * odd results (which will be none, since doubling is always even),
 * and takes the first 5. The subscriber prints what arrives.
 *
 * Demonstrates: subject, subscribe with demand, operators, backpressure.
 */

#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"
#include "ol_reactive.h"

#include <stdio.h>
#include <stdlib.h>

static int g_seen = 0;

static void on_next(void *item, void *ud) {
    (void)ud;
    int v = *(int*)item;
    printf("[subscriber] item = %d\n", v);
    g_seen++;
    free(item);
}

static void on_complete(void *ud) {
    (void)ud;
    printf("[subscriber] complete\n");
}

static void *map_double(const void *item, void *ud) {
    (void)ud;
    int v = *(const int*)item;
    int *out = malloc(sizeof(int));
    *out = v * 2;
    return out;
}

int main(void) {
    printf("OLSRT Demo 10: Reactive\n");
    printf("=======================\n\n");

    ol_event_loop_t *loop = ol_event_loop_create();
    if (!loop) return 1;

    ol_subject_t *s = ol_subject_create(loop, free);
    if (!s) { ol_event_loop_destroy(loop); return 1; }

    ol_observable_t *src = ol_subject_as_observable(s);
    ol_observable_t *mapped = ol_rx_map(src, map_double, NULL, free);
    ol_observable_t *taken  = ol_rx_take(mapped, 5);

    ol_rx_subscription_t *sub = ol_observable_subscribe(
        taken, on_next, NULL, on_complete, 100, NULL);
    if (!sub) {
        ol_observable_destroy(taken);
        ol_observable_destroy(mapped);
        ol_subject_destroy(s);
        ol_event_loop_destroy(loop);
        return 1;
    }

    printf("[main] emitting 1..8\n\n");
    for (int i = 1; i <= 8 && g_seen < 5; i++) {
        int *v = malloc(sizeof(int));
        *v = i;
        ol_subject_on_next(s, v);
    }

    printf("\n[main] seen %d items\n", g_seen);

    ol_rx_subscription_destroy(sub);
    ol_observable_destroy(taken);
    ol_observable_destroy(mapped);
    ol_subject_destroy(s);
    ol_event_loop_destroy(loop);

    return g_seen == 5 ? 0 : 2;
}
