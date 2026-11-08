/* OLSRT side: 1000 coroutine ping-pongs. Native (ol_co_*). */
#include "bench_util.h"
#include "ol_common.h"
#include "ol_coroutines.h"
#include <stdio.h>
#include <stdlib.h>

#define N 1000

static long g_consumed = 0;
static void *consumer(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++) {
        int *v = (int *)ol_co_yield(NULL);
        if (v) { g_consumed += *v; free(v); }
    }
    return NULL;
}
static void *producer(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++) {
        int *v = malloc(sizeof(int));
        *v = (i % 9) + 1;
        (void)ol_co_yield(v);
    }
    return NULL;
}

int main(void) {
    printf("olsrt/08_coroutines: native (ol_co_*)\n");
    printf("%d ping-pongs\n\n", N);
    uint64_t t0 = bench_now_ns();
    ol_coroutine_scheduler_init();
    ol_co_t *p = ol_co_spawn(producer, NULL, 64 * 1024);
    ol_co_t *c = ol_co_spawn(consumer, NULL, 64 * 1024);
    for (int i = 0; i < N; i++) {
        (void)ol_co_resume(p, NULL);
        (void)ol_co_resume(c, NULL);
    }
    (void)ol_co_join(c);
    ol_co_destroy(p);
    ol_co_destroy(c);
    ol_coroutine_scheduler_shutdown();
    printf("items_produced:     %d\n", N);
    printf("items_consumed:     %ld\n", g_consumed / 1);  /* count sum */
    printf("sum_value:          %ld\n", g_consumed);
    bench_footer(t0, N, "round_trips");
    return 0;
}
