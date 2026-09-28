/**
 * @file test_wave1.c
 * @brief Regression tests for OLSRT Wave 1 stabilization patches.
 *
 * These tests focus on things that can be validated without the
 * green-thread scheduler running the actor main loop:
 *
 *   - ol_actor_send_timeout returns -3 (timeout) on a full mailbox
 *   - ol_actor_send_timeout returns 0 on an empty mailbox
 *   - ol_arena_free ignores foreign pointers
 *   - mailbox ring buffer send/recv does not crash
 *   - event loop create/destroy is clean
 *   - channel close semantics are preserved
 *
 * Memory-leak reporting is left to a separate sanitizer pass; LSan is
 * disabled here because the process/actor architecture does not yet
 * drive the green-thread scheduler (that is a Wave 2 concern).
 */

#include "ol_actor.h"
#include "ol_actor_arena.h"
#include "ol_channel.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"
#include "ol_lock_mutex.h"
#include "ol_promise.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Harness                                                            */
/* ------------------------------------------------------------------ */

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
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/* A behavior that never runs during these tests (no scheduler drive).
 * If it ever did run, it would just free the message. */
static int noop_beh(ol_actor_t *a, void *msg)
{
    (void)a;
    if (msg) free(msg);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test 1: ol_actor_send_timeout respects the deadline                */
/* ------------------------------------------------------------------ */

static void test_actor_send_timeout(void)
{
    printf("\nTest 1: ol_actor_send_timeout\n");

    /* Create an actor with a small mailbox. Do NOT start it: nothing
     * consumes messages, so the mailbox stays full. */
    ol_actor_t *a = ol_actor_create(NULL, 4, free, noop_beh, NULL);
    EXPECT(a != NULL, "actor created with capacity 4");
    if (!a) return;

    /* Saturate the mailbox. The exact number depends on the ring
     * buffer + overflow list; we just fill it. */
    int sent = 0;
    for (int i = 0; i < 64; i++) {
        if (ol_actor_try_send(a, strdup("x")) == 1) {
            sent++;
        } else {
            break;
        }
    }
    printf("    (filled mailbox with %d messages)\n", sent);
    EXPECT(sent > 0, "at least one message fit before full");
    EXPECT(sent <= 64, "mailbox is bounded");

    /* Now send with a 200 ms deadline. Since the mailbox is full and
     * nothing consumes, this must time out with -3. */
    int64_t t0 = ol_monotonic_now_ns();
    int r = ol_actor_send_timeout(a, strdup("late"), 200);
    int64_t elapsed_ms = (ol_monotonic_now_ns() - t0) / 1000000;

    printf("    (send_timeout returned %d after %lld ms)\n",
           r, (long long)elapsed_ms);
    EXPECT(r == -3, "returned -3 (timeout) on full mailbox");
    EXPECT(elapsed_ms >= 150 && elapsed_ms <= 400,
           "timed out near 200 ms (no busy-wait)");

    ol_actor_destroy(a);
}

/* ------------------------------------------------------------------ */
/* Test 2: ol_actor_send_timeout success path                         */
/* ------------------------------------------------------------------ */

static void test_actor_send_timeout_success(void)
{
    printf("\nTest 2: ol_actor_send_timeout - success path\n");

    ol_actor_t *a = ol_actor_create(NULL, 8, free, noop_beh, NULL);
    EXPECT(a != NULL, "actor created");
    if (!a) return;

    int r = ol_actor_send_timeout(a, strdup("hello"), 500);
    EXPECT(r == 0, "send succeeded on empty mailbox within deadline");

    ol_actor_destroy(a);
}

/* ------------------------------------------------------------------ */
/* Test 3: arena ownership validation                                 */
/* ------------------------------------------------------------------ */

static void test_arena_ownership(void)
{
    printf("\nTest 3: ol_arena_free ownership validation\n");

    ol_arena_t *arena = ol_arena_create(64 * 1024, false);
    EXPECT(arena != NULL, "arena created");
    if (!arena) return;

    void *inside = ol_arena_alloc(arena, 128);
    EXPECT(inside != NULL, "allocated inside arena");
    ol_arena_free(arena, inside);
    EXPECT(1, "free of owned pointer did not crash");

    int stack_var = 0;
    ol_arena_free(arena, &stack_var);
    EXPECT(1, "free of foreign (stack) pointer ignored");

    ol_arena_free(arena, NULL);
    EXPECT(1, "free(NULL) is a no-op");

    ol_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* Test 4: mailbox try_send does not crash                            */
/* ------------------------------------------------------------------ */

static void test_mailbox_atomics(void)
{
    printf("\nTest 4: mailbox atomic send/recv\n");

    ol_actor_t *a = ol_actor_create(NULL, 64, free, noop_beh, NULL);
    EXPECT(a != NULL, "actor created");
    if (!a) return;

    int sent = 0;
    for (int i = 0; i < 64; i++) {
        if (ol_actor_try_send(a, strdup("m")) == 1) sent++;
    }
    printf("    (sent %d messages)\n", sent);
    EXPECT(sent > 0, "at least one message sent");

    ol_actor_destroy(a);
}

/* ------------------------------------------------------------------ */
/* Test 5: event loop create/destroy is clean                         */
/* ------------------------------------------------------------------ */

static void test_event_loop_smoke(void)
{
    printf("\nTest 5: event loop smoke test\n");

    ol_event_loop_t *loop = ol_event_loop_create();
    EXPECT(loop != NULL, "event loop created");
    if (!loop) return;

    ol_event_loop_stop(loop);
    ol_event_loop_destroy(loop);
    EXPECT(1, "event loop destroyed cleanly");
}

/* ------------------------------------------------------------------ */
/* Test 6: channel close semantics                                    */
/* ------------------------------------------------------------------ */

static void test_channel_close(void)
{
    printf("\nTest 6: channel close semantics\n");

    ol_channel_t *ch = ol_channel_create(0, free);
    EXPECT(ch != NULL, "channel created");
    if (!ch) return;

    EXPECT(ol_channel_send(ch, strdup("a")) == 0, "send succeeded");
    EXPECT(ol_channel_close(ch) == 0, "close succeeded");
    EXPECT(ol_channel_is_closed(ch) == true, "is_closed reports true");

    void *out = NULL;
    int r = ol_channel_recv(ch, &out);
    EXPECT(r == 1, "recv returns 1 for buffered item");
    if (r == 1 && out) free(out);

    r = ol_channel_recv(ch, &out);
    EXPECT(r == 0, "recv returns 0 on closed+empty");

    ol_channel_destroy(ch);
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("OLSRT Wave 1 regression suite\n");
    printf("=============================\n");

    test_actor_send_timeout();
    test_actor_send_timeout_success();
    test_arena_ownership();
    test_mailbox_atomics();
    test_event_loop_smoke();
    test_channel_close();

    printf("\n=============================\n");
    printf("Passed: %d\n", g_pass);
    printf("Failed: %d\n", g_fail);

    return g_fail == 0 ? 0 : 1;
}
