/**
 * @file 12_semaphores.c
 * @brief Counting semaphore across parallel workers.
 *
 * Eight tasks run on a pool of eight threads. A counting semaphore
 * with max_count = 2 limits how many run their work section at the
 * same time. The demo prints the peak concurrent count, the average
 * wait time, and the total wall clock time.
 */

#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_lock_mutex.h"
#include "ol_parallel.h"
#include "ol_semaphores.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <time.h>
#else
#include <windows.h>
#endif

#define N_WORKERS       8
#define MAX_CONCURRENT  2
#define WORK_MS         150

typedef struct {
    ol_sem_t   sem;
    ol_mutex_t mu;
    int        active;
    int        peak;
    int        done;
    int64_t    total_wait_ns;
} ctx_t;

static void sleep_ms(long ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

static void worker_fn(void *arg)
{
    ctx_t *ctx = (ctx_t *)arg;

    int64_t t0 = ol_monotonic_now_ns();
    int rc = ol_sem_wait_until(&ctx->sem,
                               ol_deadline_from_ms(5000).when_ns);
    int64_t wait_ns = ol_monotonic_now_ns() - t0;

    if (rc != 0) {
        fprintf(stderr, "  [worker] sem_wait_until failed: %d\n", rc);
        return;
    }

    ol_mutex_lock(&ctx->mu);
    ctx->active++;
    if (ctx->active > ctx->peak) ctx->peak = ctx->active;
    printf("  [worker] acquired  (active = %d/%d)\n",
           ctx->active, MAX_CONCURRENT);
    ctx->total_wait_ns += wait_ns;
    ol_mutex_unlock(&ctx->mu);

    sleep_ms(WORK_MS);

    ol_mutex_lock(&ctx->mu);
    ctx->active--;
    ctx->done++;
    printf("  [worker] releasing (active = %d/%d)\n",
           ctx->active, MAX_CONCURRENT);
    ol_mutex_unlock(&ctx->mu);

    ol_sem_post(&ctx->sem);
}

int main(void)
{
    printf("OLSRT Demo 12: Semaphores\n");
    printf("=========================\n\n");
    printf("workers        : %d\n", N_WORKERS);
    printf("max concurrent : %d\n", MAX_CONCURRENT);
    printf("work per task  : %d ms\n", WORK_MS);
    printf("expected time  : ~%d ms\n\n",
           (N_WORKERS + MAX_CONCURRENT - 1) / MAX_CONCURRENT * WORK_MS);

    ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (ol_mutex_init(&ctx.mu) != OL_SUCCESS) return 1;
    if (ol_sem_init(&ctx.sem, MAX_CONCURRENT, MAX_CONCURRENT) != 0) {
        ol_mutex_destroy(&ctx.mu);
        return 1;
    }

    ol_parallel_pool_t *pool = ol_parallel_create(N_WORKERS);
    if (!pool) {
        ol_sem_destroy(&ctx.sem);
        ol_mutex_destroy(&ctx.mu);
        return 1;
    }

    int64_t t0 = ol_monotonic_now_ns();
    for (int i = 0; i < N_WORKERS; i++) {
        if (ol_parallel_submit(pool, worker_fn, &ctx) != 0) break;
    }
    ol_parallel_flush(pool);
    int64_t t1 = ol_monotonic_now_ns();

    double total_ms    = (t1 - t0) / 1e6;
    double avg_wait_ms = (ctx.done > 0)
        ? (double)ctx.total_wait_ns / ctx.done / 1e6 : 0.0;

    printf("\nresults\n");
    printf("  completed      : %d / %d\n", ctx.done, N_WORKERS);
    printf("  peak active    : %d  (limit was %d)\n",
           ctx.peak, MAX_CONCURRENT);
    printf("  total time     : %.1f ms\n", total_ms);
    printf("  avg wait       : %.1f ms\n", avg_wait_ms);

    ol_parallel_destroy(pool);
    ol_sem_destroy(&ctx.sem);
    ol_mutex_destroy(&ctx.mu);

    if (ctx.done == N_WORKERS && ctx.peak <= MAX_CONCURRENT) {
        printf("\n[OK] all workers finished, limit respected\n");
        return 0;
    }
    printf("\n[FAIL] expectations not met\n");
    return 2;
}
