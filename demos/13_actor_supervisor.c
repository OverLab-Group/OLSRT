/**
 * @file 13_actor_supervisor.c
 * @brief Actors behind a supervisor.
 *
 * Three children, each running its own actor, are managed by a
 * supervisor. One child crashes on purpose; the supervisor restarts
 * it under the configured intensity limit.
 */

#include "ol_actor.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_supervisor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N_CHILDREN      3
#define CRASHING_CHILD  2

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static int actor_beh(ol_actor_t *a, void *msg)
{
    ol_ask_envelope_t *env = (ol_ask_envelope_t *)msg;
    if (!env || !env->reply) {
        if (msg) free(msg);
        return 0;
    }
    long id = (long)ol_actor_get_context(a);
    const char *req = (const char *)env->payload;
    printf("  [actor %ld] request: %s\n", id, req ? req : "(null)");
    free(env->payload);
    const char *reply = (id == CRASHING_CHILD) ? "crash" : "ok";
    ol_actor_reply_ok(env, strdup(reply), free);
    return 0;
}

static int child_fn(void *arg)
{
    long id = (long)arg;
    printf("  [child %ld] starting actor\n", id);

    ol_actor_t *a = ol_actor_create(NULL, 16, free, actor_beh, (void *)id);
    if (!a) return -1;
    ol_actor_start(a);

    ol_future_t *f = ol_actor_ask(a, strdup("ping"));
    if (!f) { ol_actor_stop(a); ol_actor_destroy(a); return -1; }

    int exit_code = 0;
    if (ol_future_await(f, ol_deadline_from_ms(1000).when_ns) == 1) {
        const char *v = (const char *)ol_future_get_value_const(f);
        if (v && strcmp(v, "crash") == 0) {
            printf("  [child %ld] actor requested crash\n", id);
            exit_code = 1;
        }
    }
    ol_future_destroy(f);
    ol_actor_stop(a);
    ol_actor_destroy(a);
    printf("  [child %ld] exiting with code %d\n", id, exit_code);
    return exit_code;
}

int main(void)
{
    printf("OLSRT Demo 13: Actor behind Supervisor\n");
    printf("======================================\n\n");
    printf("children       : %d\n", N_CHILDREN);
    printf("crashing child : %d\n\n", CRASHING_CHILD);

    ol_supervisor_config_t cfg = ol_supervisor_default_config();
    cfg.strategy          = OL_SUP_ONE_FOR_ONE;
    cfg.max_restarts      = 3;
    cfg.restart_window_ms = 2000;
    cfg.enable_logging    = false;

    ol_supervisor_t *sup = ol_supervisor_create(&cfg);
    if (!sup) return 1;

    (void)ol_supervisor_start(sup);

    for (long i = 1; i <= N_CHILDREN; i++) {
        ol_child_spec_t spec = ol_child_spec_create(
            "actor_child", child_fn, (void *)i,
            OL_CHILD_PERMANENT, 1000);
        uint32_t id = ol_supervisor_add_child(sup, &spec);
        printf("[main] added child id=%u (arg=%ld)\n", id, i);
    }

    printf("\n[main] letting the tree run for 600 ms\n\n");
    sleep_ms(600);

    ol_supervisor_stats_t stats;
    if (ol_supervisor_get_stats(sup, &stats) == 0) {
        printf("\nsupervisor stats\n");
        printf("  children          : %zu\n", stats.child_count);
        printf("  total restarts    : %llu\n",
               (unsigned long long)stats.total_restarts);
        printf("  total crashes     : %llu\n",
               (unsigned long long)stats.total_crashes);
        printf("  uptime            : %llu ms\n",
               (unsigned long long)stats.uptime_ms);
    }

    ol_supervisor_stop(sup, true);
    ol_supervisor_destroy(sup);
    printf("\n[OK] supervisor demo completed\n");
    return 0;
}
