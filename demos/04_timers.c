/**
 * @file 04_timers.c
 * @brief Event loop + timer demo.
 *
 * Registers three timers on one event loop:
 *   - one-shot at 20 ms
 *   - periodic every 60 ms
 *   - stop timer at 400 ms
 *
 * Measures timer accuracy (interval between periodic fires) and
 * confirms callbacks run in order without a busy-wait.
 */

#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"

#include <stdio.h>
#include <stdlib.h>

static int64_t g_t0              = 0;
static int     g_oneshot_fired   = 0;
static int     g_periodic_count  = 0;
static int64_t g_last_periodic_ns = 0;

static void oneshot_cb(ol_event_loop_t* loop, ol_ev_type_t type,
                       int fd, void* ud) {
    (void)loop; (void)type; (void)fd; (void)ud;
    int64_t dt = (ol_monotonic_now_ns() - g_t0) / 1000000;
    printf("[t=%4lld ms] one-shot fired\n", (long long)dt);
    g_oneshot_fired = 1;
}

static void periodic_cb(ol_event_loop_t* loop, ol_ev_type_t type,
                        int fd, void* ud) {
    (void)loop; (void)type; (void)fd; (void)ud;
    int64_t now = ol_monotonic_now_ns();
    int64_t dt  = (now - g_t0) / 1000000;
    int64_t delta_us = g_last_periodic_ns
        ? (now - g_last_periodic_ns) / 1000
        : 0;
    g_last_periodic_ns = now;
    g_periodic_count++;
    printf("[t=%4lld ms] periodic #%d  (interval = %lld us)\n",
           (long long)dt, g_periodic_count, (long long)delta_us);
}

static void stop_cb(ol_event_loop_t* loop, ol_ev_type_t type,
                    int fd, void* ud) {
    (void)type; (void)fd; (void)ud;
    int64_t dt = (ol_monotonic_now_ns() - g_t0) / 1000000;
    printf("[t=%4lld ms] stop timer fired -> stopping loop\n", (long long)dt);
    ol_event_loop_stop(loop);
}

int main(void) {
    printf("OLSRT Demo 04: Event Loop + Timers\n");
    printf("===================================\n\n");

    ol_event_loop_t* loop = ol_event_loop_create();
    if (!loop) { fprintf(stderr, "loop create failed\n"); return 1; }

    g_t0 = ol_monotonic_now_ns();

    ol_event_loop_register_timer(loop,
        ol_deadline_from_ms(20),  0,
        oneshot_cb, NULL);

    ol_event_loop_register_timer(loop,
        ol_deadline_from_ms(60),  60 * 1000000LL,
        periodic_cb, NULL);

    ol_event_loop_register_timer(loop,
        ol_deadline_from_ms(400), 0,
        stop_cb, NULL);

    printf("loop running; expect 1 one-shot, >= 3 periodic, then stop\n\n");
    ol_event_loop_run(loop);

    int64_t total = (ol_monotonic_now_ns() - g_t0) / 1000000;
    printf("\nloop stopped after %lld ms\n", (long long)total);
    printf("one-shot fired : %s\n", g_oneshot_fired ? "yes" : "no");
    printf("periodic count : %d\n", g_periodic_count);

    ol_event_loop_destroy(loop);

    if (g_oneshot_fired && g_periodic_count >= 3) {
        printf("\n[OK] timers fired as expected\n");
        return 0;
    }
    printf("\n[FAIL] timer expectations not met\n");
    return 2;
}
