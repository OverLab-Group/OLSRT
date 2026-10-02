/**
 * @file test_channel_advanced.c
 * @brief Extended channel tests.
 *
 * Covers the paths the testing docs claim but test_wave1.c does not:
 *   - bounded capacity behaviour
 *   - try_send / try_recv semantics on full / empty
 *   - send_deadline / recv_deadline timing
 *   - item destructor invoked on closed-channel send failure
 */

#include "ol_common.h"
#include "ol_channel.h"
#include "ol_deadlines.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Destructor that counts how many items it freed. */
static int g_dtor_calls = 0;
static void counting_dtor(void *p)
{
    if (p) {
        free(p);
        g_dtor_calls++;
    }
}

/* ------------------------------------------------------------------ */

static void test_capacity(void)
{
    printf("\nTest 1: bounded capacity\n");

    ol_channel_t *ch = ol_channel_create(3, counting_dtor);
    EXPECT(ch != NULL, "channel created");
    EXPECT(ol_channel_capacity(ch) == 3, "capacity == 3");
    EXPECT(ol_channel_len(ch) == 0, "initial len == 0");

    EXPECT(ol_channel_send(ch, strdup("a")) == OL_SUCCESS, "send 1");
    EXPECT(ol_channel_send(ch, strdup("b")) == OL_SUCCESS, "send 2");
    EXPECT(ol_channel_send(ch, strdup("c")) == OL_SUCCESS, "send 3");
    EXPECT(ol_channel_len(ch) == 3, "len == 3 after 3 sends");

    int r = ol_channel_try_send(ch, strdup("d"));
    EXPECT(r == 0, "try_send returns 0 when full");

    /* The item passed to a failed try_send is not consumed. We must
     * free it ourselves. */
    /* (The next line would leak if try_send had taken ownership.) */

    ol_channel_destroy(ch);
}

static void test_try_recv_empty(void)
{
    printf("\nTest 2: try_recv on empty channel\n");

    ol_channel_t *ch = ol_channel_create(0, NULL);
    EXPECT(ch != NULL, "unbounded channel created");
    EXPECT(ol_channel_capacity(ch) == 0, "capacity == 0 (unbounded)");

    void *out = (void *)0xdeadbeef;
    int r = ol_channel_try_recv(ch, &out);
    EXPECT(r == 0, "try_recv returns 0 on empty");
    EXPECT(out == NULL, "out pointer cleared");

    ol_channel_destroy(ch);
}

static void test_send_deadline_timeout(void)
{
    printf("\nTest 3: send_deadline times out on full channel\n");

    ol_channel_t *ch = ol_channel_create(2, NULL);

    /* Fill it. */
    ol_channel_send(ch, strdup("x"));
    ol_channel_send(ch, strdup("y"));

    int64_t t0 = ol_monotonic_now_ns();
    int r = ol_channel_send_deadline(
        ch, strdup("z"), ol_deadline_from_ms(80).when_ns);
    int64_t elapsed_ms = (ol_monotonic_now_ns() - t0) / 1000000;

    EXPECT(r == OL_TIMEOUT, "send_deadline returned OL_TIMEOUT");
    EXPECT(elapsed_ms >= 50 && elapsed_ms <= 300,
           "waited approximately 80 ms");

    /* The item passed to a timed-out send is not consumed either. */

    ol_channel_destroy(ch);
}

static void test_recv_deadline_timeout(void)
{
    printf("\nTest 4: recv_deadline times out on empty channel\n");

    ol_channel_t *ch = ol_channel_create(0, NULL);

    int64_t t0 = ol_monotonic_now_ns();
    void *out = (void *)0xdeadbeef;
    int r = ol_channel_recv_deadline(
        ch, &out, ol_deadline_from_ms(60).when_ns);
    int64_t elapsed_ms = (ol_monotonic_now_ns() - t0) / 1000000;

    EXPECT(r == OL_TIMEOUT, "recv_deadline returned OL_TIMEOUT");
    EXPECT(elapsed_ms >= 30 && elapsed_ms <= 250,
           "waited approximately 60 ms");
    EXPECT(out == NULL, "out pointer cleared on timeout");

    ol_channel_destroy(ch);
}

static void test_destructor_on_close(void)
{
    printf("\nTest 5: destructor on closed send\n");

    ol_channel_t *ch = ol_channel_create(0, counting_dtor);
    ol_channel_close(ch);

    g_dtor_calls = 0;
    int r = ol_channel_send(ch, strdup("dropped"));
    EXPECT(r == OL_CLOSED, "send on closed channel returns OL_CLOSED");
    EXPECT(g_dtor_calls == 1, "destructor invoked once for the dropped item");

    ol_channel_destroy(ch);
}

static void test_fifo_order(void)
{
    printf("\nTest 6: FIFO order\n");

    ol_channel_t *ch = ol_channel_create(0, free);
    for (int i = 1; i <= 5; i++) {
        int *v = (int *)malloc(sizeof(int));
        *v = i;
        ol_channel_send(ch, v);
    }

    int ok = 1;
    for (int i = 1; i <= 5; i++) {
        void *out = NULL;
        if (ol_channel_recv(ch, &out) != 1) { ok = 0; break; }
        if (out == NULL || *(int *)out != i) { ok = 0; free(out); break; }
        free(out);
    }
    EXPECT(ok, "items received in FIFO order");

    ol_channel_destroy(ch);
}

int main(void)
{
    printf("Channel (advanced) test suite\n");
    printf("=============================\n");

    test_capacity();
    test_try_recv_empty();
    test_send_deadline_timeout();
    test_recv_deadline_timeout();
    test_destructor_on_close();
    test_fifo_order();

    printf("\n=============================\n");
    printf("Passed: %d\n", g_pass);
    printf("Failed: %d\n", g_fail);

    return g_fail == 0 ? 0 : 1;
}
