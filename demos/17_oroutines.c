/**
 * @file 17_oroutines.c
 * @brief 100 oroutines, 10 channels, 100k messages (v1.3.4.1 demo).
 *
 * Pipeline:
 *   - 10 producer oroutines, each pushes 10,000 ints into one of
 *     10 shared channels (round-robin).
 *   - 10 relay oroutines drain those 10 channels and forward into
 *     one central consumer channel.
 *   - 1 consumer oroutine drains the central channel and sums.
 *
 * Total: 100,000 messages through 11 channels, 21 oroutines.
 */

#include "oroutines.h"
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t bench_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}



#define N_PRODUCERS      10
#define N_RELAYS         10
#define N_CHANNELS       10
#define MSGS_PER_PRODUCER 10000
#define TOTAL_MSGS       (N_PRODUCERS * MSGS_PER_PRODUCER)

static or_chan_t* g_prod_chans[N_CHANNELS];
static or_chan_t* g_central;

static long g_total_recv = 0;
static long g_sum        = 0;

static void producer_fn(void* arg) {
    long idx = (long)arg;
    or_chan_t* c = g_prod_chans[idx % N_CHANNELS];
    for (int i = 0; i < MSGS_PER_PRODUCER; i++) {
        int v = (int)(idx * 100 + i);
        or_chan_send(c, &v);
    }
}

static void relay_fn(void* arg) {
    long idx = (long)arg;
    or_chan_t* src = g_prod_chans[idx];
    while (1) {
        int v;
        int rc = or_chan_recv(src, &v);
        if (rc != 0) break;
        or_chan_send(g_central, &v);
    }
}

static void consumer_fn(void* arg) {
    (void)arg;
    while (1) {
        int v;
        int rc = or_chan_recv(g_central, &v);
        if (rc != 0) break;
        g_total_recv++;
        g_sum += v;
    }
}

int main(void) {
    printf("OLSRT Demo 17: ORoutines\n");
    printf("=========================\n\n");
    printf("producers      : %d\n", N_PRODUCERS);
    printf("relays         : %d\n", N_RELAYS);
    printf("channels       : %d prod + 1 central\n", N_CHANNELS);
    printf("messages       : %d\n\n", TOTAL_MSGS);

    uint64_t t0 = bench_now_ns();

    /* Create channels. */
    for (int i = 0; i < N_CHANNELS; i++) {
        g_prod_chans[i] = or_chan_create(sizeof(int), 64);
        if (!g_prod_chans[i]) { fprintf(stderr, "chan %d failed\n", i); return 1; }
    }
    g_central = or_chan_create(sizeof(int), 128);

    /* Spawn consumer first (so it drains). */
    oroutine_t* consumer = or_go(consumer_fn, NULL);

    /* Spawn relays. */
    for (long i = 0; i < N_RELAYS; i++)
        or_go(relay_fn, (void*)i);

    /* Spawn producers. */
    for (long i = 0; i < N_PRODUCERS; i++)
        or_go(producer_fn, (void*)i);

    /* Run until all oroutines are done or blocked. */
    int n = or_run();
    printf("[main] or_run returned %d\n", n);

    /* Close all channels to unblock relays/consumer if still waiting. */
    for (int i = 0; i < N_CHANNELS; i++) or_chan_close(g_prod_chans[i]);
    or_chan_close(g_central);

    /* Run again to let blocked oroutines finish. */
    or_run();
    (void)consumer;

    uint64_t elapsed = bench_now_ns() - t0;

    printf("\nmessages received : %ld\n", g_total_recv);
    printf("sum               : %ld\n", g_sum);
    printf("elapsed           : %.2f ms\n", elapsed / 1e6);
    printf("throughput        : %.0f msg/s\n",
           elapsed > 0 ? g_total_recv / (elapsed / 1e9) : 0);

    for (int i = 0; i < N_CHANNELS; i++) or_chan_destroy(g_prod_chans[i]);
    or_chan_destroy(g_central);

    if (g_total_recv == TOTAL_MSGS) {
        printf("\n[OK] all %d messages delivered\n", TOTAL_MSGS);
            return 0;
    }
    printf("\n[FAIL] expected %d, got %ld\n", TOTAL_MSGS, g_total_recv);
    return 2;
}
