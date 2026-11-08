/* OLSRT side: supervisor with 3 children, 1 crashes. NO sleep. Native. */
#include "bench_util.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_supervisor.h"
#include <stdio.h>

static int worker(void *arg) {
    long id = (long)arg;
    if (id == 1) return 42;
    return 0;
}

int main(void) {
    printf("olsrt/09_supervisor: native (ol_supervisor)\n");
    printf("3 children, child 1 crashes, restart policy\n\n");
    uint64_t t0 = bench_now_ns();
    ol_supervisor_config_t cfg = ol_supervisor_default_config();
    cfg.enable_logging = false;
    cfg.max_restarts = 5;
    cfg.restart_window_ms = 5000;
    ol_supervisor_t *sup = ol_supervisor_create(&cfg);
    ol_supervisor_start(sup);
    for (long i = 1; i <= 3; i++) {
        ol_child_spec_t spec = ol_child_spec_create(
            "w", worker, (void *)i, OL_CHILD_PERMANENT, 500);
        ol_supervisor_add_child(sup, &spec);
    }
    /* Wait for children to complete / restart */
    for (int i = 0; i < 50; i++) {
        struct timespec ts = { 0, 10000000 };  /* 10 ms */
        nanosleep(&ts, NULL);
    }
    ol_supervisor_stats_t st = { 0 };
    ol_supervisor_get_stats(sup, &st);
    ol_supervisor_stop(sup, true);
    ol_supervisor_destroy(sup);
    printf("children:           %zu\n", st.child_count);
    printf("total_restarts:     %llu\n", (unsigned long long)st.total_restarts);
    printf("total_crashes:      %llu\n", (unsigned long long)st.total_crashes);
    bench_footer(t0, 1, "supervisor_run");
    return 0;
}
