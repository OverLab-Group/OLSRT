/* libuv side: demand counter + direct callback. Built (~10 LoC). */
#include "bench_util.h"
#include <stdio.h>

#define N 1000
static int g_received = 0, demand = 1;
static void on_next(int v) {
    if (demand <= 0) return;
    demand--;
    volatile long long work = 0;
    for (int _i = 0; _i < 100; _i++) work += (long long)v * _i;
    (void)work;
    g_received++;
    demand++;
}

int main(void) {
    printf("libuv/11_streams: built (demand counter)\n");
    printf("%d items with demand=1 backpressure\n\n", N);
    uint64_t t0 = bench_now_ns();
    for (int i = 1; i <= N; i++) on_next(i);
    printf("items_emitted:      %d\n", N);
    printf("items_received:     %d\n", g_received);
    bench_footer(t0, N, "items");
    return 0;
}
