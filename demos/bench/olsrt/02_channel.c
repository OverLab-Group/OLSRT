/* OLSRT side: 1M messages through bounded channel. Native. */
#include "bench_util.h"
#include "ol_channel.h"
#include "ol_parallel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 1000000
#define CAP 1024

typedef struct { ol_channel_t *ch; long long produced; } ctx_t;

static void prod(void *arg) {
    ctx_t *c = arg;
    for (long long i = 0; i < N; i++) {
        char *m = malloc(24);
        snprintf(m, 24, "msg-%lld", i);
        if (ol_channel_send(c->ch, m) != 0) { free(m); break; }
        c->produced++;
    }
    ol_channel_close(c->ch);
}

int main(void) {
    printf("olsrt/02_channel: native (ol_channel_t)\n");
    printf("%d messages, cap=%d\n\n", N, CAP);
    uint64_t t0 = bench_now_ns();
    ol_channel_t *ch = ol_channel_create(CAP, free);
    ol_parallel_pool_t *pool = ol_parallel_create(2);
    ctx_t c = { ch, 0 };
    ol_parallel_submit(pool, prod, &c);
    long long received = 0;
    void *m;
    while (ol_channel_recv(ch, &m) == 1) { free(m); received++; }
    ol_parallel_flush(pool);
    ol_parallel_destroy(pool);
    ol_channel_destroy(ch);
    printf("messages_sent:      %lld\n", c.produced);
    printf("messages_received:  %lld\n", received);
    printf("message_size_bytes: 24\n");
    printf("bytes_total:        %lld\n", c.produced * 24);
    printf("channel_capacity:   %d\n", CAP);
    bench_footer(t0, c.produced, "messages");
    return 0;
}
