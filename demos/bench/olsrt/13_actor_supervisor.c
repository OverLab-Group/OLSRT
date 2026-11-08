/* OLSRT side: 3 child actors, 1 crashes. NO sleep. Native. */
#include "bench_util.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_supervisor.h"
#include <stdio.h>
#include <stdlib.h>

#define N 3
static int ok_children = 0;

static int child(void *arg) {
    long id = (long)arg;
    if (id == 2) return 42;  /* crash */
    ok_children++;
    return 0;
}

int main(void) {
    printf("olsrt/13_actor_supervisor: native (supervisor + children)\n");
    printf("%d children, 1 crashes\n\n", N);
    uint64_t t0 = bench_now_ns();
    ol_supervisor_config_t cfg = ol_supervisor_default_config();
    cfg.enable_logging = false;
    cfg.max_restarts = 3;
    ol_supervisor_t *sup = ol_supervisor_create(&cfg);
    ol_supervisor_start(sup);
    for (long i = 1; i <= N; i++) {
        ol_child_spec_t spec = ol_child_spec_create(
            "c", child, (void *)i, OL_CHILD_PERMANENT, 500);
        ol_supervisor_add_child(sup, &spec);
    }
    for (int i = 0; i < 30; i++) {
        struct timespec ts = { 0, 10000000 };
        nanosleep(&ts, NULL);
    }
    ol_supervisor_stats_t st = { 0 };
    ol_supervisor_get_stats(sup, &st);
    ol_supervisor_stop(sup, true);
    ol_supervisor_destroy(sup);
    printf("children:           %d\n", N);
    printf("crashes:            %llu\n", (unsigned long long)st.total_crashes);
    printf("restarts:           %llu\n", (unsigned long long)st.total_restarts);
    printf("successful_children:%d\n", ok_children);
    bench_footer(t0, N, "children");
    return 0;
}
