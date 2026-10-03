/**
 * @file 15_reactive_loop.c
 * @brief Event loop feeding a subject.
 *
 * A periodic timer pushes integers into a subject; a subscriber
 * receives them. After N_TICKS the subject completes and the loop
 * stops.
 *
 * The map/filter/take operator chain in v1.3.2 is incomplete (an
 * operator stores a reference to its source but never subscribes to
 * it), so this demo wires the timer directly to the subject.
 */

#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"
#include "ol_reactive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N_TICKS        5
#define TICK_PERIOD_MS 120

static int g_received = 0;
static int g_next_value = 1;

static void on_next(void *item, void *ud)
{
    (void)ud;
    int v = item ? *(int *)item : -1;
    g_received++;
    printf("[subscriber] tick %d  value = %d\n", g_received, v);
}

static void on_complete(void *ud)
{
    ol_event_loop_t *loop = (ol_event_loop_t *)ud;
    printf("[subscriber] complete\n");
    ol_event_loop_stop(loop);
}

static void timer_cb(ol_event_loop_t *loop, ol_ev_type_t type,
                     int fd, void *ud)
{
    (void)type; (void)fd;
    ol_subject_t *subject = (ol_subject_t *)ud;
    int *v = (int *)malloc(sizeof(int));
    if (!v) return;
    *v = g_next_value++;
    ol_subject_on_next(subject, v);
    if (g_received >= N_TICKS) {
        ol_subject_on_complete(subject);
        ol_event_loop_stop(loop);
    }
}

int main(void)
{
    printf("OLSRT Demo 15: Event Loop -> Subject -> Subscriber\n");
    printf("===================================================\n\n");
    printf("ticks          : %d\n", N_TICKS);
    printf("period         : %d ms\n\n", TICK_PERIOD_MS);

    ol_event_loop_t *loop = ol_event_loop_create();
    if (!loop) return 1;

    ol_subject_t *subject = ol_subject_create(loop, free);
    if (!subject) { ol_event_loop_destroy(loop); return 1; }

    ol_observable_t *obs = ol_subject_as_observable(subject);

    ol_rx_subscription_t *sub = ol_observable_subscribe(
        obs, on_next, NULL, on_complete, 100, loop);
    if (!sub) {
        ol_subject_destroy(subject);
        ol_event_loop_destroy(loop);
        return 1;
    }

    ol_event_loop_register_timer(
        loop,
        ol_deadline_from_ms(TICK_PERIOD_MS),
        TICK_PERIOD_MS * 1000000LL,
        timer_cb, subject);

    printf("loop running\n\n");
    ol_event_loop_run(loop);

    ol_rx_subscription_destroy(sub);
    ol_subject_destroy(subject);
    ol_event_loop_destroy(loop);

    printf("\nreceived %d ticks\n", g_received);
    if (g_received == N_TICKS) {
        printf("[OK] subject delivered %d values\n", N_TICKS);
        return 0;
    }
    printf("[FAIL] expected %d, got %d\n", N_TICKS, g_received);
    return 2;
}
