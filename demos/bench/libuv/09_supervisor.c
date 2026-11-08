/* libuv side: uv_spawn 3 shells, restart on failure. Built (~60 LoC). */
#include "bench_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#define N_CHILDREN 3
#define MAX_RESTARTS 5

typedef struct {
    uv_process_t proc;
    uv_process_options_t opts;
    char *args[4];
    int id, restarts, done;
} child_t;

static uv_loop_t *g_loop;
static int total_restarts = 0, total_crashes = 0, running = 0;

static void on_child_exit(uv_process_t *p, int64_t code, int sig) {
    (void)sig;
    child_t *c = (child_t *)p->data;
    if (code != 0) total_crashes++;
    if (code != 0 && c->restarts < MAX_RESTARTS) {
        c->restarts++;
        total_restarts++;
        uv_spawn(g_loop, &c->proc, &c->opts);
        return;
    }
    c->done = 1;
    uv_close((uv_handle_t *)&c->proc, NULL);
    running--;
    if (running == 0) uv_stop(g_loop);
}

int main(void) {
    printf("libuv/09_supervisor: built (uv_spawn + restart loop)\n");
    printf("3 shells, child 1 crashes, restart policy\n\n");
    uint64_t t0 = bench_now_ns();
    g_loop = uv_default_loop();
    child_t ch[N_CHILDREN];
    for (int i = 0; i < N_CHILDREN; i++) {
        memset(&ch[i], 0, sizeof(ch[i]));
        ch[i].id = i;
        ch[i].opts.exit_cb = on_child_exit;
        ch[i].opts.file = "/bin/sh";
        ch[i].args[0] = "sh"; ch[i].args[1] = "-c";
        ch[i].args[2] = (i == 1) ? "exit 1" : "exit 0";
        ch[i].args[3] = NULL;
        ch[i].opts.args = ch[i].args;
        ch[i].proc.data = &ch[i];
        uv_spawn(g_loop, &ch[i].proc, &ch[i].opts);
        running++;
    }
    uv_run(g_loop, UV_RUN_DEFAULT);
    printf("children:           %d\n", N_CHILDREN);
    printf("total_restarts:     %d\n", total_restarts);
    printf("total_crashes:      %d\n", total_crashes);
    uv_loop_close(g_loop);
    bench_footer(t0, 1, "supervisor_run");
    return 0;
}
