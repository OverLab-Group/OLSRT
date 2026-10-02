/**
 * @file test_promise.c
 * @brief Promise / Future tests.
 *
 * Covers the paths the testing docs claim:
 *   - fulfill, then get value
 *   - reject, then get error code
 *   - cancel
 *   - take_value ownership transfer
 *   - .then() continuation on already-resolved future
 *   - await on an already-resolved future returns immediately
 *   - await times out on a pending future
 */

#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_promise.h"

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

/* ------------------------------------------------------------------ */

static void test_fulfill(void)
{
    printf("\nTest 1: fulfill\n");

    ol_promise_t *p = ol_promise_create(NULL);
    ol_future_t  *f = ol_promise_get_future(p);
    EXPECT(p != NULL && f != NULL, "promise and future created");

    EXPECT(ol_promise_state(p) == OL_PROMISE_PENDING, "initial state pending");
    EXPECT(!ol_promise_is_done(p), "is_done false");

    int r = ol_promise_fulfill(p, strdup("hello"), free);
    EXPECT(r == OL_SUCCESS, "fulfill returned OL_SUCCESS");
    EXPECT(ol_promise_is_done(p), "promise is done");
    EXPECT(ol_future_state(f) == OL_PROMISE_FULFILLED, "state fulfilled");

    int ar = ol_future_await(f, 0);
    EXPECT(ar == 1, "await on resolved future returns 1 immediately");

    const char *v = (const char *)ol_future_get_value_const(f);
    EXPECT(v != NULL && strcmp(v, "hello") == 0, "get_value_const returns the string");

    ol_future_destroy(f);
    ol_promise_destroy(p);
}

static void test_reject(void)
{
    printf("\nTest 2: reject\n");

    ol_promise_t *p = ol_promise_create(NULL);
    ol_future_t  *f = ol_promise_get_future(p);

    int r = ol_promise_reject(p, -42);
    EXPECT(r == OL_SUCCESS, "reject returned OL_SUCCESS");
    EXPECT(ol_future_state(f) == OL_PROMISE_REJECTED, "state rejected");
    EXPECT(ol_future_error(f) == -42, "error code is -42");
    EXPECT(ol_future_get_value_const(f) == NULL, "no value on rejected future");

    ol_future_destroy(f);
    ol_promise_destroy(p);
}

static void test_cancel(void)
{
    printf("\nTest 3: cancel\n");

    ol_promise_t *p = ol_promise_create(NULL);
    ol_future_t  *f = ol_promise_get_future(p);

    int r = ol_promise_cancel(p);
    EXPECT(r == OL_SUCCESS, "cancel returned OL_SUCCESS");
    EXPECT(ol_future_state(f) == OL_PROMISE_CANCELED, "state canceled");
    EXPECT(ol_future_error(f) == 0, "no error on canceled future");

    /* Second resolution must fail. */
    EXPECT(ol_promise_fulfill(p, NULL, NULL) != OL_SUCCESS,
           "fulfill after cancel fails");
    EXPECT(ol_promise_reject(p, -1) != OL_SUCCESS,
           "reject after cancel fails");

    ol_future_destroy(f);
    ol_promise_destroy(p);
}

static void test_take_value(void)
{
    printf("\nTest 4: take_value transfers ownership\n");

    ol_promise_t *p = ol_promise_create(NULL);
    ol_future_t  *f = ol_promise_get_future(p);

    ol_promise_fulfill(p, strdup("owned"), free);

    void *v = ol_future_take_value(f);
    EXPECT(v != NULL, "take_value returned the buffer");
    EXPECT(strcmp((char *)v, "owned") == 0, "contents preserved");
    EXPECT(ol_future_get_value_const(f) == NULL,
           "get_value_const returns NULL after take");

    /* Second take must not return the same buffer. */
    void *v2 = ol_future_take_value(f);
    EXPECT(v2 == NULL, "second take returns NULL");

    free(v);   /* we own it now */
    ol_future_destroy(f);
    ol_promise_destroy(p);
}

static void test_await_timeout(void)
{
    printf("\nTest 5: await times out on a pending future\n");

    ol_promise_t *p = ol_promise_create(NULL);
    ol_future_t  *f = ol_promise_get_future(p);

    int64_t t0 = ol_monotonic_now_ns();
    int r = ol_future_await(f, ol_deadline_from_ms(50).when_ns);
    int64_t elapsed_ms = (ol_monotonic_now_ns() - t0) / 1000000;

    EXPECT(r == OL_TIMEOUT, "await returned OL_TIMEOUT");
    EXPECT(elapsed_ms >= 30 && elapsed_ms <= 250,
           "waited approximately 50 ms");

    ol_future_destroy(f);
    ol_promise_destroy(p);
}

/* Continuation callback counter. */
static int g_then_calls = 0;
static char g_then_value[64];

static void on_resolved(ol_event_loop_t *loop,
                        ol_promise_state_t st,
                        const void *value,
                        int err,
                        void *ud)
{
    (void)loop; (void)ud;
    g_then_calls++;
    if (st == OL_PROMISE_FULFILLED && value) {
        strncpy(g_then_value, (const char *)value, sizeof(g_then_value) - 1);
    } else if (st == OL_PROMISE_REJECTED) {
        snprintf(g_then_value, sizeof(g_then_value), "err=%d", err);
    }
}

static void test_then_after_resolution(void)
{
    printf("\nTest 6: .then() on an already-resolved future\n");

    ol_promise_t *p = ol_promise_create(NULL);
    ol_future_t  *f = ol_promise_get_future(p);

    ol_promise_fulfill(p, strdup("later"), free);

    g_then_calls = 0;
    g_then_value[0] = 0;
    int r = ol_future_then(f, on_resolved, NULL);
    EXPECT(r == OL_SUCCESS, "then registered");
    EXPECT(g_then_calls == 1, "continuation invoked immediately");
    EXPECT(strcmp(g_then_value, "later") == 0,
           "continuation received the value");

    ol_future_destroy(f);
    ol_promise_destroy(p);
}

static void test_then_before_resolution(void)
{
    printf("\nTest 7: .then() before resolution\n");

    ol_promise_t *p = ol_promise_create(NULL);
    ol_future_t  *f = ol_promise_get_future(p);

    g_then_calls = 0;
    g_then_value[0] = 0;

    ol_future_then(f, on_resolved, NULL);
    EXPECT(g_then_calls == 0, "continuation not yet invoked");

    ol_promise_fulfill(p, strdup("now"), free);
    EXPECT(g_then_calls == 1, "continuation invoked on resolution");
    EXPECT(strcmp(g_then_value, "now") == 0, "value delivered");

    ol_future_destroy(f);
    ol_promise_destroy(p);
}

int main(void)
{
    printf("Promise / Future test suite\n");
    printf("===========================\n");

    test_fulfill();
    test_reject();
    test_cancel();
    test_take_value();
    test_await_timeout();
    test_then_after_resolution();
    test_then_before_resolution();

    printf("\n===========================\n");
    printf("Passed: %d\n", g_pass);
    printf("Failed: %d\n", g_fail);

    return g_fail == 0 ? 0 : 1;
}
