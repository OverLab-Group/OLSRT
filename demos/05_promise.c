/**
 * @file 05_promise.c
 * @brief Promise / Future continuation demo.
 *
 * Demonstrates four patterns:
 *   1. fulfill + then (continuation fires on resolve)
 *   2. reject  + then (continuation sees the error code)
 *   3. attach a continuation AFTER resolving (fires immediately)
 *   4. multiple continuations on the same future
 */

#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_promise.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_cb_count = 0;

static void on_resolved(ol_event_loop_t* loop,
                        ol_promise_state_t st,
                        const void* value,
                        int err,
                        void* ud) {
    (void)loop;
    const char* name = (const char*)ud;
    g_cb_count++;

    switch (st) {
        case OL_PROMISE_FULFILLED:
            printf("  [%d] %-10s : FULFILLED   value='%s'\n",
                   g_cb_count, name,
                   value ? (const char*)value : "(null)");
            break;
        case OL_PROMISE_REJECTED:
            printf("  [%d] %-10s : REJECTED    err=%d\n",
                   g_cb_count, name, err);
            break;
        case OL_PROMISE_CANCELED:
            printf("  [%d] %-10s : CANCELED\n", g_cb_count, name);
            break;
        default:
            printf("  [%d] %-10s : PENDING?\n", g_cb_count, name);
            break;
    }
}

int main(void) {
    printf("OLSRT Demo 05: Promise + Continuations\n");
    printf("=======================================\n\n");

    printf("Case 1: fulfill with a value\n");
    {
        ol_promise_t* p = ol_promise_create(NULL);
        ol_future_t*  f = ol_promise_get_future(p);
        ol_future_then(f, on_resolved, "case1");
        ol_promise_fulfill(p, strdup("hello world"), free);
        int r = ol_future_await(f, ol_deadline_from_ms(200).when_ns);
        printf("  await returned : %d\n", r);
        ol_future_destroy(f);
        ol_promise_destroy(p);
    }

    printf("\nCase 2: reject with an error code\n");
    {
        ol_promise_t* p = ol_promise_create(NULL);
        ol_future_t*  f = ol_promise_get_future(p);
        ol_future_then(f, on_resolved, "case2");
        ol_promise_reject(p, -42);
        int r = ol_future_await(f, ol_deadline_from_ms(200).when_ns);
        printf("  await returned : %d\n", r);
        printf("  error code     : %d\n", ol_future_error(f));
        ol_future_destroy(f);
        ol_promise_destroy(p);
    }

    printf("\nCase 3: register continuation AFTER fulfill\n");
    {
        ol_promise_t* p = ol_promise_create(NULL);
        ol_future_t*  f = ol_promise_get_future(p);
        ol_promise_fulfill(p, strdup("late observer"), free);
        ol_future_then(f, on_resolved, "case3");
        ol_future_destroy(f);
        ol_promise_destroy(p);
    }

    printf("\nCase 4: multiple continuations on one future\n");
    {
        ol_promise_t* p = ol_promise_create(NULL);
        ol_future_t*  f = ol_promise_get_future(p);
        ol_future_then(f, on_resolved, "multi-a");
        ol_future_then(f, on_resolved, "multi-b");
        ol_future_then(f, on_resolved, "multi-c");
        ol_promise_fulfill(p, strdup("shared"), free);
        ol_future_destroy(f);
        ol_promise_destroy(p);
    }

    printf("\ntotal callbacks invoked: %d\n", g_cb_count);

    if (g_cb_count == 6) {
        printf("\n[OK] all continuations fired as expected\n");
        return 0;
    }
    printf("\n[FAIL] expected 6 callbacks, saw %d\n", g_cb_count);
    return 2;
}
