/* OLSRT side: 1000 items through channel with actor consumer. Native. */
#include "bench_util.h"
#include "ol_actor.h"
#include "ol_channel.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_parallel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 1000
static long g_squared_sum = 0;
static int g_consumed = 0;

static int consumer_beh(ol_actor_t *a, void *msg) {
    (void)msg;
    ol_channel_t *ch = ol_actor_get_context(a);
    void *item = NULL;
    while (ol_channel_recv(ch, &item) == 1) {
        int v = *(int *)item;
        g_squared_sum += v * v;
        g_consumed++;
        free(item);
    }
    return 1;
}
static void producer(void *arg) {
    ol_channel_t *ch = arg;
    for (int i = 1; i <= N; i++) {
        int *v = malloc(sizeof(int));
        *v = i;
        ol_channel_send(ch, v);
    }
    ol_channel_close(ch);
}

int main(void) {
    printf("olsrt/14_actor_channel: native (actor + channel)\n");
    printf("%d items through channel\n\n", N);
    uint64_t t0 = bench_now_ns();
    ol_channel_t *ch = ol_channel_create(0, free);
    ol_actor_t *actor = ol_actor_create(NULL, 16, free, consumer_beh, ch);
    ol_actor_start(actor);
    ol_parallel_pool_t *pool = ol_parallel_create(2);
    ol_parallel_submit(pool, producer, ch);
    ol_actor_send(actor, strdup("go"));
    ol_parallel_flush(pool);
    for (int i = 0; i < 100 && g_consumed < N; i++) {
        struct timespec ts = { 0, 10000000 };
        nanosleep(&ts, NULL);
    }
    ol_parallel_destroy(pool);
    ol_actor_stop(actor);
    ol_actor_destroy(actor);
    ol_channel_destroy(ch);
    printf("items_sent:         %d\n", N);
    printf("items_consumed:     %d\n", g_consumed);
    printf("sum_of_squares:     %ld\n", g_squared_sum);
    bench_footer(t0, N, "items");
    return 0;
}
