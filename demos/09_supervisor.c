/**
 * @file 09_supervisor.c
 * @brief Supervision tree with restart strategies.
 *
 * @details
 * A supervisor watches three children. One child crashes on purpose,
 * the supervisor restarts it under the configured policy, and the
 * demo prints the restart count.
 */

#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_supervisor.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int worker_fn(void *arg) {
    long id = (long)arg;
    printf("  [child %ld] start\n", id);
    struct timespec ts = { 0, 50 * 1000 * 1000 };
    nanosleep(&ts, NULL);
    if (id == 2) {
        printf("  [child %ld] intentionally crashing\n", id);
        return 42;
    }
    printf("  [child %ld] normal exit\n", id);
    return 0;
}

int main(void) {
    printf("OLSRT Demo 09: Supervisor\n");
    printf("=========================\n\n");

    ol_supervisor_config_t cfg = ol_supervisor_default_config();
    cfg.strategy       = OL_SUP_ONE_FOR_ONE;
    cfg.max_restarts   = 5;
    cfg.restart_window_ms = 5000;

    ol_supervisor_t *sup = ol_supervisor_create(&cfg);
    if (!sup) {
        fprintf(stderr, "supervisor create failed\n");
        return 1;
    }
    (void)ol_supervisor_start(sup);

    for (long i = 1; i <= 3; i++) {
        ol_child_spec_t spec = ol_child_spec_create(
            "worker", worker_fn, (void*)i,
            OL_CHILD_PERMANENT, 2000);
        uint32_t id = ol_supervisor_add_child(sup, &spec);
        printf("[main] added child id=%u\n", id);
    }

    printf("\n[main] letting the tree run for 400 ms\n\n");
    struct timespec ts = { 0, 400 * 1000 * 1000 };
    nanosleep(&ts, NULL);

    ol_supervisor_stats_t stats;
    if (ol_supervisor_get_stats(sup, &stats) == 0) {
        printf("\n[main] supervisor stats\n");
        printf("       children          : %zu\n", stats.child_count);
        printf("       total restarts    : %llu\n",
               (unsigned long long)stats.total_restarts);
        printf("       total crashes     : %llu\n",
               (unsigned long long)stats.total_crashes);
    }

    ol_supervisor_stop(sup, true);
    ol_supervisor_destroy(sup);

    printf("\n[OK] supervisor demo completed\n");
    return 0;
}
