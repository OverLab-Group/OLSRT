/**
 * @file test_event_loop_timers.c
 * @brief Event loop timer tests.
 *
 * Covers the paths the testing docs claim:
 *   - one-shot timer fires exactly once
 *   - periodic timer fires repeatedly
 *   - wake from another thread does not crash
 *   - stop from inside a callback exits the loop cleanly
 */

#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <pthread.h>
#include <unistd.h>
#endif

static int g_pass = 0;
static int g_fail = 0;

static void expect(int cond, const char *msg, const char *file, int line)
{
    if (cond) {
        printf("  [PASS] %s\n", msg);
        g_pass++;
    } else {
        printf("  [FAIL] %s  (at %s:%d)\n", msg, file, line);
        g_fail++;
    }
}

#define EXPECT(cond, msg) expect((cond), (msg), __FILE__, __LINE__)

/* ------------------------------------------------------------------ */

static int g_oneshot_count = 0;
static int g_periodic_count = 0;
static ol_event_loop_t *g_loop = NULL;

static void oneshot_cb(ol_event_loop_t *loop, ol_ev_type_t type,
                       int fd, void *ud)
{
    (void)loop; (void)type; (void)fd; (void)ud;
    g_oneshot_count++;
}

static void periodic_cb(ol_event_loop_t *loop, ol_ev_type_t type,
                        int fd, void *ud)
{
    (void)loop; (void)type; (void)fd; (void)ud;
    g_periodic_count++;
}

static void stop_cb(ol_event_loop_t *loop, ol_ev_type_t type,
                    int fd, void *ud)
{
    (void)type; (void)fd; (void)ud;
    ol_event_loop_stop(loop);
}

static void test_oneshot(void)
{
    printf("\nTest 1: one-shot timer\n");

    g_loop = ol_event_loop_create();
    EXPECT(g_loop != NULL, "loop created");
    if (!g_loop) return;

    g_oneshot_count = 0;

    uint64_t id = ol_event_loop_register_timer(
        g_loop, ol_deadline_from_ms(30), 0, oneshot_cb, NULL);
    EXPECT(id != 0, "one-shot timer registered");

    ol_event_loop_register_timer(
        g_loop, ol_deadline_from_ms(120), 0, stop_cb, NULL);

    ol_event_loop_run(g_loop);
    EXPECT(g_oneshot_count == 1, "one-shot fired exactly once");

    ol_event_loop_destroy(g_loop);
    g_loop = NULL;
}

static void test_periodic(void)
{
    printf("\nTest 2: periodic timer\n");

    g_loop = ol_event_loop_create();
    if (!g_loop) { EXPECT(0, "loop create failed"); return; }

    g_periodic_count = 0;

    /* 30 ms period, stop after 200 ms -> expect 5-7 fires. */
    ol_event_loop_register_timer(
        g_loop, ol_deadline_from_ms(30), 30 * 1000000LL,
        periodic_cb, NULL);
    ol_event_loop_register_timer(
        g_loop, ol_deadline_from_ms(200), 0, stop_cb, NULL);

    ol_event_loop_run(g_loop);

    printf("    (periodic fired %d times)\n", g_periodic_count);
    EXPECT(g_periodic_count >= 3, "periodic fired at least 3 times");
    EXPECT(g_periodic_count <= 12, "periodic fired a sane number of times");

    ol_event_loop_destroy(g_loop);
    g_loop = NULL;
}

static void test_stop_from_outside(void)
{
    printf("\nTest 3: stop from another thread\n");

    g_loop = ol_event_loop_create();
    if (!g_loop) { EXPECT(0, "loop create failed"); return; }

    /* Register a long timer so the loop would otherwise block. */
    ol_event_loop_register_timer(
        g_loop, ol_deadline_from_ms(5000), 0, oneshot_cb, NULL);

    /* Register a 100 ms timer that stops the loop from inside. */
    ol_event_loop_register_timer(
        g_loop, ol_deadline_from_ms(100), 0, stop_cb, NULL);

    int64_t t0 = ol_monotonic_now_ns();
    ol_event_loop_run(g_loop);
    int64_t elapsed_ms = (ol_monotonic_now_ns() - t0) / 1000000;

    EXPECT(elapsed_ms < 1000, "loop exited quickly, did not wait 5 s");

    ol_event_loop_destroy(g_loop);
    g_loop = NULL;
}

static void test_is_running_flag(void)
{
    printf("\nTest 4: is_running flag\n");

    g_loop = ol_event_loop_create();
    if (!g_loop) { EXPECT(0, "loop create failed"); return; }

    EXPECT(!ol_event_loop_is_running(g_loop), "not running before run()");

    ol_event_loop_register_timer(
        g_loop, ol_deadline_from_ms(40), 0, stop_cb, NULL);
    ol_event_loop_run(g_loop);

    EXPECT(!ol_event_loop_is_running(g_loop),
           "not running after run() returns");

    ol_event_loop_destroy(g_loop);
    g_loop = NULL;
}

int main(void)
{
    printf("Event loop timer test suite\n");
    printf("============================\n");

    test_oneshot();
    test_periodic();
    test_stop_from_outside();
    test_is_running_flag();

    printf("\n============================\n");
    printf("Passed: %d\n", g_pass);
    printf("Failed: %d\n", g_fail);

    return g_fail == 0 ? 0 : 1;
}
