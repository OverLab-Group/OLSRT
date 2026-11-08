/* libuv side: pthread mutex+condvar FIFO. Built (~80 LoC in header). */
#include "bench_util.h"
#include "bench_channel.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define N 1000000
#define CAP 1024

typedef struct { bench_channel_t *ch; long long produced; } ctx_t;

static void *prod(void *arg) {
    ctx_t *c = arg;
    for (long long i = 0; i < N; i++) {
        char *m = malloc(24);
        snprintf(m, 24, "msg-%lld", i);
        bc_send(c->ch, m);
        c->produced++;
    }
    bc_close(c->ch);
    return NULL;
}

int main(void) {
    printf("libuv/02_channel: built (pthread mutex+condvar FIFO)\n");
    printf("%d messages, cap=%d\n\n", N, CAP);
    uint64_t t0 = bench_now_ns();
    bench_channel_t ch;
    bc_init(&ch, CAP);
    ctx_t c = { &ch, 0 };
    pthread_t t;
    pthread_create(&t, NULL, prod, &c);
    long long received = 0;
    void *m;
    while ((m = bc_recv(&ch)) != NULL) { free(m); received++; }
    pthread_join(t, NULL);
    bc_destroy(&ch);
    printf("messages_sent:      %lld\n", c.produced);
    printf("messages_received:  %lld\n", received);
    printf("message_size_bytes: 24\n");
    printf("bytes_total:        %lld\n", c.produced * 24);
    printf("channel_capacity:   %d\n", CAP);
    bench_footer(t0, c.produced, "messages");
    return 0;
}
