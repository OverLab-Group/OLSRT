/* libuv side: two pthread stages with channels. Built (~30 LoC on top of bench_channel). */
#include "bench_util.h"
#include "bench_channel.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define N 1000
typedef struct { bench_channel_t *in, *out; } stage_t;
static int g_sink_count = 0, g_sink_last = 0;

static void *dbl_fn(void *arg) {
    stage_t *s = arg;
    int *v;
    while ((v = bc_recv(s->in)) != NULL) {
        int *o = malloc(sizeof(int));
        *o = *v * 2;
        free(v);
        bc_send(s->out, o);
    }
    bc_close(s->out);
    return NULL;
}

int main(void) {
    printf("libuv/06_dataflow: built (2 pthreads + 2 channels)\n");
    printf("%d items through source->doubler->sink\n\n", N);
    uint64_t t0 = bench_now_ns();
    bench_channel_t q1, q2;
    bc_init(&q1, 0);  /* v3: unbounded to avoid deadlock */
    bc_init(&q2, 0);  /* v3: unbounded */
    stage_t s = { &q1, &q2 };
    pthread_t dth;
    pthread_create(&dth, NULL, dbl_fn, &s);

    /* Source: push N items. Sink: read from q2. Interleave. */
    pthread_t th;
    (void)th;
    for (int i = 1; i <= N; i++) {
        int *v = malloc(sizeof(int)); *v = i;
        bc_send(&q1, v);
    }
    bc_close(&q1);

    int *out;
    while ((out = bc_recv(&q2)) != NULL) {
        g_sink_count++;
        g_sink_last = *out;
        free(out);
    }
    pthread_join(dth, NULL);
    bc_destroy(&q1); bc_destroy(&q2);
    printf("items_pushed:       %d\n", N);
    printf("items_sunk:         %d\n", g_sink_count);
    printf("last_value:         %d (expect %d)\n", g_sink_last, N * 2);
    bench_footer(t0, N, "items");
    return 0;
}
