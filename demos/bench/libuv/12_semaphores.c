/* libuv side: uv_sem_t. Native. */
#include "bench_util.h"
#include <stdio.h>
#include <uv.h>

#define NW 8
#define MAXC 2
#define WMS 100

static uv_sem_t sem;
static uv_mutex_t mu;
static int active = 0, peak = 0, done = 0, wait_ms = 0;

static void worker(void *arg) {
    (void)arg;
    uint64_t t0 = bench_now_ns();
    uv_sem_wait(&sem);
    int w = (int)((bench_now_ns() - t0) / 1000000);
    uv_mutex_lock(&mu);
    active++;
    if (active > peak) peak = active;
    wait_ms += w;
    uv_mutex_unlock(&mu);

    uv_sleep(WMS);

    uv_mutex_lock(&mu);
    active--;
    done++;
    uv_mutex_unlock(&mu);
    uv_sem_post(&sem);
}

int main(void) {
    printf("libuv/12_semaphores: native (uv_sem_t)\n");
    printf("%d threads, max_concurrent=%d, work=%dms\n\n", NW, MAXC, WMS);
    uint64_t t0 = bench_now_ns();
    uv_sem_init(&sem, MAXC);
    uv_mutex_init(&mu);
    uv_thread_t th[NW];
    for (int i = 0; i < NW; i++) uv_thread_create(&th[i], worker, NULL);
    for (int i = 0; i < NW; i++) uv_thread_join(&th[i]);
    uv_sem_destroy(&sem);
    uv_mutex_destroy(&mu);
    printf("threads_total:      %d\n", NW);
    printf("threads_done:       %d\n", done);
    printf("peak_concurrent:    %d (limit %d)\n", peak, MAXC);
    printf("mean_wait_ms:       %.1f\n", done ? (double)wait_ms / done : 0);
    bench_footer(t0, NW, "semaphore_ops");
    return 0;
}
