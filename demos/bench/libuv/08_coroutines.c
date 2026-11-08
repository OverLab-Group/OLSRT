/* libuv side: 2 pthreads, ping-pong via mutex+condvar. Built (~50 LoC). */
#include "bench_util.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define N 1000
static pthread_mutex_t mu;
static pthread_cond_t cv;
static int turn = 0;
static int current = 0;
static long g_sum = 0;

static void *producer(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++) {
        pthread_mutex_lock(&mu);
        while (turn != 0) pthread_cond_wait(&cv, &mu);
        current = (i % 9) + 1;
        turn = 1;
        pthread_cond_broadcast(&cv);
        pthread_mutex_unlock(&mu);
    }
    return NULL;
}
static void *consumer(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++) {
        pthread_mutex_lock(&mu);
        while (turn != 1) pthread_cond_wait(&cv, &mu);
        g_sum += current;
        turn = 0;
        pthread_cond_broadcast(&cv);
        pthread_mutex_unlock(&mu);
    }
    return NULL;
}

int main(void) {
    printf("libuv/08_coroutines: built (pthread ping-pong)\n");
    printf("%d ping-pongs\n\n", N);
    uint64_t t0 = bench_now_ns();
    pthread_mutex_init(&mu, NULL);
    pthread_cond_init(&cv, NULL);
    pthread_t p, c;
    pthread_create(&p, NULL, producer, NULL);
    pthread_create(&c, NULL, consumer, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
    pthread_mutex_destroy(&mu);
    pthread_cond_destroy(&cv);
    printf("items_produced:     %d\n", N);
    printf("items_consumed:     %d\n", N);
    printf("sum_value:          %ld\n", g_sum);
    bench_footer(t0, N, "round_trips");
    return 0;
}
