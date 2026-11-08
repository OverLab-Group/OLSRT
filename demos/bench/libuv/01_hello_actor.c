/* libuv side: 1000 ask/reply via uv_queue_work. Built (no actor). */
#include "bench_util.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#define N 1000

typedef struct {
    uv_work_t work;
    char *input, *output;
    int done;
    uint64_t t_submit, t_complete;
} req_t;

static void work_cb(uv_work_t *w) {
    req_t *r = w->data;
    strcpy(r->output, r->input);
    for (char *p = r->output; *p; p++) *p = (char)toupper((unsigned char)*p);
}
static void after_cb(uv_work_t *w, int status) {
    (void)status;
    req_t *r = w->data;
    r->t_complete = bench_now_ns();
    r->done = 1;
}

int main(void) {
    printf("libuv/01_hello_actor: built (uv_queue_work per request)\n");
    printf("%d round-trips\n\n", N);
    uint64_t t0 = bench_now_ns();
    uv_loop_t *loop = uv_default_loop();
    const char *w[5] = { "hello", "world", "olsrt", "actor", "model" };
    uint64_t sum = 0, mn = UINT64_MAX, mx = 0;
    int ok = 0;
    for (int i = 0; i < N; i++) {
        req_t r = { 0 };
        r.input = strdup(w[i % 5]);
        r.output = calloc(64, 1);
        r.t_submit = bench_now_ns();
        r.work.data = &r;
        uv_queue_work(loop, &r.work, work_cb, after_cb);
        while (!r.done) uv_run(loop, UV_RUN_ONCE);
        uint64_t d = r.t_complete - r.t_submit;
        sum += d; if (d < mn) mn = d; if (d > mx) mx = d;
        ok++;
        free(r.input); free(r.output);
    }
    printf("requests_sent:      %d\n", N);
    printf("replies_received:   %d\n", ok);
    printf("rtt_mean_us:        %.2f\n", (double)sum / ok / 1000.0);
    printf("rtt_min_us:         %.2f\n", (double)mn / 1000.0);
    printf("rtt_max_us:         %.2f\n", (double)mx / 1000.0);
    bench_footer(t0, N, "round_trips");
    uv_loop_close(loop);
    return 0;
}
