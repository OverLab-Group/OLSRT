/**
 * @file test_v133_fixes.c
 * @brief Regression tests for the OLSRT v1.3.3 fix bundle.
 *
 * Build (from project root):
 *     make -C demos
 *     make -f Makefile.v133 run
 *
 * v2: g_sink_value in Test 3 is now guarded by a mutex instead of
 * C11 atomics. On x86_64, atomic release-store / acquire-load lower
 * to plain MOV instructions; TSan sees the MOVs and reports a false
 * positive. A mutex is slower but unambiguous, and Test 3 is a
 * correctness check, not a benchmark.
 */

#include "ol_actor.h"
#include "ol_common.h"
#include "ol_dataflow.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"
#include "ol_lock_mutex.h"
#include "ol_promise.h"
#include "ol_reactive.h"
#include "ol_supervisor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *msg, const char *file, int line)
{
    if (cond) {
        printf("  [PASS] %s\n", msg);
        g_pass++;
    } else {
        printf("  [FAIL] %s  (%s:%d)\n", msg, file, line);
        g_fail++;
    }
}

#define CHECK(c, m) check((c), (m), __FILE__, __LINE__)

static void sleep_ms(long ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

/* ------------------------------------------------------------------ */
/* Test 1 — reply_error double free                                    */
/* ------------------------------------------------------------------ */

static int error_beh(ol_actor_t *a, void *msg)
{
    (void)a;
    ol_ask_envelope_t *env = (ol_ask_envelope_t *)msg;
    if (!env || !env->reply) { if (msg) free(msg); return 0; }
    if (env->payload) free(env->payload);
    ol_actor_reply_error(env, -42);
    return 0;
}

static void test_reply_error_no_double_free(void)
{
    printf("\nTest 1: ol_actor_reply_error does not double-free\n");
    ol_actor_t *a = ol_actor_create(NULL, 16, free, error_beh, NULL);
    CHECK(a != NULL, "actor created");
    if (!a) return;
    CHECK(ol_actor_start(a) == 0, "actor started");
    sleep_ms(50);
    ol_future_t *f = ol_actor_ask(a, strdup("trigger"));
    CHECK(f != NULL, "ask returned a future");
    if (!f) { ol_actor_destroy(a); return; }
    int r = ol_future_await(f, ol_deadline_from_ms(1000).when_ns);
    CHECK(r == 1, "future resolved within 1 s");
    if (r == 1) {
        CHECK(ol_future_state(f) == OL_PROMISE_REJECTED,
              "future state is REJECTED");
        CHECK(ol_future_error(f) == -42, "error code is -42");
    }
    ol_future_destroy(f);
    ol_actor_stop(a);
    ol_actor_destroy(a);
    CHECK(1, "no crash");
}

/* ------------------------------------------------------------------ */
/* Test 2 — supervisor IDs unique                                     */
/* ------------------------------------------------------------------ */

static int dummy_child(void *arg) { (void)arg; return 0; }

static void test_supervisor_unique_ids(void)
{
    printf("\nTest 2: supervisor child IDs are unique\n");
    ol_supervisor_config_t cfg = ol_supervisor_default_config();
    cfg.enable_logging = false;
    ol_supervisor_t *s1 = ol_supervisor_create(&cfg);
    ol_supervisor_t *s2 = ol_supervisor_create(&cfg);
    CHECK(s1 != NULL && s2 != NULL, "two supervisors created");
    if (!s1 || !s2) {
        if (s1) ol_supervisor_destroy(s1);
        if (s2) ol_supervisor_destroy(s2);
        return;
    }
    ol_child_spec_t spec = ol_child_spec_create(
        "c", dummy_child, NULL, OL_CHILD_TEMPORARY, 100);
    uint32_t id1 = ol_supervisor_add_child(s1, &spec);
    uint32_t id2 = ol_supervisor_add_child(s2, &spec);
    uint32_t id3 = ol_supervisor_add_child(s1, &spec);
    CHECK(id1 != 0 && id2 != 0 && id3 != 0, "three IDs non-zero");
    CHECK(id1 != id2, "IDs across supervisors are distinct");
    CHECK(id1 != id3, "IDs within same supervisor are distinct");
    CHECK(id2 != id3, "IDs across supervisors are distinct (2)");
    ol_supervisor_stop(s1, true);
    ol_supervisor_stop(s2, true);
    ol_supervisor_destroy(s1);
    ol_supervisor_destroy(s2);
}

/* ------------------------------------------------------------------ */
/* Test 3 — dataflow multi-hop (mutex-protected sink)                 */
/* ------------------------------------------------------------------ */

static ol_mutex_t g_sink_mutex;
static int        g_sink_value = -1;

static void sink_reset(void)
{
    ol_mutex_lock(&g_sink_mutex);
    g_sink_value = -1;
    ol_mutex_unlock(&g_sink_mutex);
}

static void sink_set(int v)
{
    ol_mutex_lock(&g_sink_mutex);
    g_sink_value = v;
    ol_mutex_unlock(&g_sink_mutex);
}

static int sink_get(void)
{
    int v;
    ol_mutex_lock(&g_sink_mutex);
    v = g_sink_value;
    ol_mutex_unlock(&g_sink_mutex);
    return v;
}

static int df_sink_handler(void *ctx, void *item,
                           int (*emit)(void *, int, void *), void *emit_ctx)
{
    (void)ctx; (void)emit; (void)emit_ctx;
    if (item) {
        sink_set(*(int *)item);
        free(item);
    }
    return 0;
}

static int df_doubler_handler(void *ctx, void *item,
                              int (*emit)(void *, int, void *),
                              void *emit_ctx)
{
    (void)ctx;
    int v = *(int *)item;
    free(item);
    int *out = (int *)malloc(sizeof(int));
    if (!out) return -1;
    *out = v * 2;
    (void)emit(emit_ctx, 0, out);
    return 0;
}

static int df_passthrough_handler(void *ctx, void *item,
                                  int (*emit)(void *, int, void *),
                                  void *emit_ctx)
{
    (void)ctx;
    (void)emit(emit_ctx, 0, item);
    return 0;
}

static void test_dataflow_multihop(void)
{
    printf("\nTest 3: dataflow multi-hop delivery (21 -> 42)\n");

    if (ol_mutex_init(&g_sink_mutex) != OL_SUCCESS) {
        CHECK(0, "mutex init failed");
        return;
    }
    sink_reset();

    ol_df_graph_t *g = ol_df_graph_create(2);
    CHECK(g != NULL, "graph created");
    if (!g) { ol_mutex_destroy(&g_sink_mutex); return; }

    ol_df_node_t *src = ol_df_node_create(g, df_passthrough_handler, NULL, 1);
    ol_df_node_t *mid = ol_df_node_create(g, df_doubler_handler,     NULL, 1);
    ol_df_node_t *snk = ol_df_node_create(g, df_sink_handler,        NULL, 0);
    CHECK(src != NULL && mid != NULL && snk != NULL, "three nodes created");
    if (!src || !mid || !snk) {
        ol_df_graph_destroy(g);
        ol_mutex_destroy(&g_sink_mutex);
        return;
    }

    ol_df_edge_t *e1 = ol_df_connect(g, src, 0, mid, 32, free);
    ol_df_edge_t *e2 = ol_df_connect(g, mid, 0, snk, 32, free);
    CHECK(e1 != NULL && e2 != NULL, "two edges connected");

    ol_df_graph_start(g);

    int *v = (int *)malloc(sizeof(int));
    *v = 21;
    ol_df_push(g, src, v);

    for (int i = 0; i < 200; i++) {
        if (sink_get() != -1) break;
        sleep_ms(5);
    }

    CHECK(sink_get() == 42,
          "sink received the doubled value (42 == 21 * 2)");

    ol_df_graph_stop(g);
    ol_df_disconnect(g, e2);
    ol_df_disconnect(g, e1);
    ol_df_graph_destroy(g);
    ol_mutex_destroy(&g_sink_mutex);
}

/* ------------------------------------------------------------------ */
/* Test 4 — subject                                                   */
/* ------------------------------------------------------------------ */

static int g_subject_next_value = -1;
static void on_subject_next(void *item, void *ud)
{
    (void)ud;
    if (item) g_subject_next_value = *(int *)item;
}

static void test_subject_lifecycle(void)
{
    printf("\nTest 4: subject create / destroy\n");
    ol_event_loop_t *loop = ol_event_loop_create();
    CHECK(loop != NULL, "event loop created");
    if (!loop) return;
    ol_subject_t *s = ol_subject_create(loop, free);
    CHECK(s != NULL, "subject created");
    if (!s) { ol_event_loop_destroy(loop); return; }
    ol_observable_t *obs = ol_subject_as_observable(s);
    CHECK(obs != NULL, "observable view obtained");
    CHECK((void *)obs == (void *)s, "same object");
    ol_rx_subscription_t *sub = ol_observable_subscribe(
        obs, on_subject_next, NULL, NULL, 4, NULL);
    CHECK(sub != NULL, "subscribed");
    int *a = (int *)malloc(sizeof(int)); *a = 10;
    int *b = (int *)malloc(sizeof(int)); *b = 20;
    ol_subject_on_next(s, a);
    ol_subject_on_next(s, b);
    CHECK(g_subject_next_value == 20, "subscriber received last value");
    ol_rx_subscription_destroy(sub);
    ol_subject_destroy(s);
    ol_event_loop_destroy(loop);
    CHECK(1, "destroyed cleanly");
}

/* ------------------------------------------------------------------ */
/* Test 5 — 32 actors                                                 */
/* ------------------------------------------------------------------ */

static int noop_beh(ol_actor_t *a, void *msg)
{
    (void)a;
    if (msg) free(msg);
    return 0;
}

static void test_actor_start_stop_cond(void)
{
    printf("\nTest 5: actor start/stop with condvar\n");
    ol_actor_t *actors[32];
    for (int i = 0; i < 32; i++)
        actors[i] = ol_actor_create(NULL, 8, free, noop_beh, NULL);
    int created = 0;
    for (int i = 0; i < 32; i++) if (actors[i]) created++;
    CHECK(created == 32, "all 32 actors created");
    sleep_ms(50);
    int stopped = 0;
    for (int i = 0; i < 32; i++) {
        if (!actors[i]) continue;
        ol_actor_start(actors[i]);
        ol_actor_stop(actors[i]);
        stopped++;
    }
    CHECK(stopped == 32, "all 32 actors stopped");
    for (int i = 0; i < 32; i++)
        if (actors[i]) ol_actor_destroy(actors[i]);
    CHECK(1, "no hang, no crash");
}

/* ------------------------------------------------------------------ */
/* Test 6 — idempotent start                                          */
/* ------------------------------------------------------------------ */

static void test_actor_start_idempotent(void)
{
    printf("\nTest 6: ol_actor_start is idempotent\n");
    ol_actor_t *a = ol_actor_create(NULL, 4, free, noop_beh, NULL);
    CHECK(a != NULL, "actor created");
    if (!a) return;
    CHECK(ol_actor_start(a) == 0, "first start returned 0");
    CHECK(ol_actor_start(a) == 0, "second start returned 0");
    CHECK(ol_actor_is_running(a), "actor reports running");
    ol_actor_stop(a);
    ol_actor_destroy(a);
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("OLSRT v1.3.3 regression suite\n");
    printf("==============================\n");
    test_reply_error_no_double_free();
    test_supervisor_unique_ids();
    test_dataflow_multihop();
    test_subject_lifecycle();
    test_actor_start_stop_cond();
    test_actor_start_idempotent();
    printf("\n==============================\n");
    printf("Passed: %d\n", g_pass);
    printf("Failed: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}

/* END OF FILE test_v133_fixes.c */
