/**
 * @file 02_channel.c
 * @brief Channel producer/consumer demo.
 *
 * Two worker threads share a bounded channel:
 *   - producer pushes N messages
 *   - consumer drains until the channel is closed
 * Measures wall-clock throughput and verifies counts.
 */

#include "ol_channel.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_parallel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N_MESSAGES   1000000LL
#define CH_CAPACITY  1024

typedef struct {
    ol_channel_t* ch;
    long long     produced;
    long long     received;
} ctx_t;

static void producer_fn(void* arg) {
    ctx_t* c = (ctx_t*)arg;
    for (long long i = 0; i < N_MESSAGES; i++) {
        char* msg = (char*)malloc(24);
        if (!msg) break;
        snprintf(msg, 24, "msg-%lld", i);

        if (ol_channel_send(c->ch, msg) != 0) {
            free(msg);
            break;
        }
        c->produced++;
    }
    /* Closing signals the consumer that no more data is coming. */
    ol_channel_close(c->ch);
}

static void consumer_fn(void* arg) {
    ctx_t* c = (ctx_t*)arg;
    void* out = NULL;
    int r;
    while ((r = ol_channel_recv(c->ch, &out)) == 1) {
        free(out);
        c->received++;
    }
    /* r == 0 means closed and empty; anything else is an error. */
}

int main(void) {
    printf("OLSRT Demo 02: Channel Producer/Consumer\n");
    printf("=========================================\n\n");
    printf("messages     : %lld\n", (long long)N_MESSAGES);
    printf("channel cap  : %d\n", CH_CAPACITY);
    printf("workers      : 2\n\n");

    ol_channel_t* ch = ol_channel_create(CH_CAPACITY, free);
    if (!ch) { fprintf(stderr, "channel create failed\n"); return 1; }

    ol_parallel_pool_t* pool = ol_parallel_create(2);
    if (!pool) {
        ol_channel_destroy(ch);
        fprintf(stderr, "pool create failed\n");
        return 1;
    }

    ctx_t ctx = { ch, 0, 0 };

    int64_t t0 = ol_monotonic_now_ns();

    ol_parallel_submit(pool, producer_fn, &ctx);
    ol_parallel_submit(pool, consumer_fn, &ctx);
    ol_parallel_flush(pool);

    int64_t t1 = ol_monotonic_now_ns();
    double dt_ms   = (double)(t1 - t0) / 1e6;
    double per_sec = (double)N_MESSAGES / (dt_ms / 1000.0);

    printf("produced     : %lld\n", ctx.produced);
    printf("received     : %lld\n", ctx.received);
    printf("elapsed      : %.2f ms\n", dt_ms);
    printf("throughput   : %.0f msg/s\n", per_sec);
    printf("in channel   : %zu items\n", ol_channel_len(ch));
    printf("closed       : %s\n", ol_channel_is_closed(ch) ? "yes" : "no");

    ol_parallel_destroy(pool);
    ol_channel_destroy(ch);

    if (ctx.produced == N_MESSAGES && ctx.received == N_MESSAGES) {
        printf("\n[OK] all messages sent and received\n");
        return 0;
    }
    printf("\n[FAIL] message count mismatch\n");
    return 2;
}
