/* libuv side: producer thread + consumer thread with channel. Built (~25 LoC). */
#include "bench_util.h"
#include "bench_channel.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define N 1000
static long g_squared_sum = 0;
static int g_consumed = 0;

static void *producer(void *arg) {
    bench_channel_t *ch = arg;
    for (int i = 1; i <= N; i++) {
        int *v = malloc(sizeof(int));
        *v = i;
        bc_send(ch, v);
    }
    bc_close(ch);
    return NULL;
}
static void *consumer(void *arg) {
    bench_channel_t *ch = arg;
    int *v;
    while ((v = bc_recv(ch)) != NULL) {
        g_squared_sum += *v * *v;
        g_consumed++;
        free(v);
    }
    return NULL;
}

int main(void) {
    printf("libuv/14_actor_channel: built (producer/consumer threads)\n");
    printf("%d items through channel\n\n", N);
    uint64_t t0 = bench_now_ns();
    bench_channel_t ch;
    bc_init(&ch, 0);
    pthread_t p, c;
    pthread_create(&c, NULL, consumer, &ch);
    pthread_create(&p, NULL, producer, &ch);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
    bc_destroy(&ch);
    printf("items_sent:         %d\n", N);
    printf("items_consumed:     %d\n", g_consumed);
    printf("sum_of_squares:     %ld\n", g_squared_sum);
    bench_footer(t0, N, "items");
    return 0;
}
