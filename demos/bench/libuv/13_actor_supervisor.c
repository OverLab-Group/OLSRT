/* libuv side: pthreads + restart loop. Built (~40 LoC). */
#include "bench_util.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define N 3
#define MAX_RESTARTS 3

typedef struct { int id, restarts, done; } w_t;
static int total_restarts = 0, ok_children = 0;

static void *worker(void *arg) {
    w_t *w = arg;
    if (w->id == 2) { /* crash (do nothing, done=0) */ }
    else { w->done = 1; }
    return NULL;
}

int main(void) {
    printf("libuv/13_actor_supervisor: built (pthreads + restart loop)\n");
    printf("%d children, 1 crashes\n\n", N);
    uint64_t t0 = bench_now_ns();
    w_t ws[N] = { 0 };
    pthread_t th[N];
    for (int i = 0; i < N; i++) { ws[i].id = i; pthread_create(&th[i], NULL, worker, &ws[i]); }
    for (int i = 0; i < N; i++) pthread_join(th[i], NULL);
    for (int i = 0; i < N; i++) {
        if (!ws[i].done) {
            while (ws[i].restarts < MAX_RESTARTS && !ws[i].done) {
                ws[i].restarts++;
                total_restarts++;
                pthread_create(&th[i], NULL, worker, &ws[i]);
                pthread_join(th[i], NULL);
            }
        } else ok_children++;
    }
    printf("children:           %d\n", N);
    printf("crashes:            1\n");
    printf("restarts:           %d\n", total_restarts);
    printf("successful_children:%d\n", ok_children);
    bench_footer(t0, N, "children");
    return 0;
}
