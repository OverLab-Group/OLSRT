/**
 * @file 03_parallel.c
 * @brief Parallel pool demo: many CPU-bound tasks on a fixed pool.
 *
 * Submits N_TASKS tasks to a pool of N_WORKERS threads. Each task:
 *   - records which worker thread ran it
 *   - does a small amount of arithmetic
 *   - increments a shared counter under a mutex
 *
 * Measures total wall-clock time, per-worker task distribution, and
 * effective parallelism.
 */

#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_lock_mutex.h"
#include "ol_parallel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <windows.h>
  #define OL_GET_TID() ((unsigned long)GetCurrentThreadId())
#else
  #include <pthread.h>
  #include <unistd.h>
  #include <sys/syscall.h>
  #define OL_GET_TID() ((unsigned long)syscall(SYS_gettid))
#endif

#define N_TASKS   100
#define N_WORKERS 4

/* ---------- shared state ---------- */

static ol_mutex_t  g_mutex;
static int         g_counter      = 0;
static int         g_done         = 0;

/* Per-worker task counter, indexed by worker slot. We map each OS
 * thread to a slot the first time we see its TID. */
#define MAX_SLOTS 64
static unsigned long g_tids[MAX_SLOTS];
static int           g_slot_count = 0;
static int           g_slot_hits[MAX_SLOTS];

static int slot_for_current_thread(void) {
    unsigned long tid = OL_GET_TID();
    ol_mutex_lock(&g_mutex);
    for (int i = 0; i < g_slot_count; i++) {
        if (g_tids[i] == tid) { ol_mutex_unlock(&g_mutex); return i; }
    }
    int s = -1;
    if (g_slot_count < MAX_SLOTS) {
        s = g_slot_count;
        g_tids[s] = tid;
        g_slot_hits[s] = 0;
        g_slot_count++;
    }
    ol_mutex_unlock(&g_mutex);
    return s;
}

/* ---------- task ---------- */

typedef struct {
    int id;
} task_arg_t;

static void task_fn(void* raw) {
    task_arg_t* a = (task_arg_t*)raw;

    int slot = slot_for_current_thread();

    /* A small CPU-bound loop: sum of squares. */
    volatile long long acc = 0;
    for (long long i = 0; i < 200000; i++) {
        acc += i * i;
    }
    (void)acc;

    ol_mutex_lock(&g_mutex);
    g_counter++;
    if (slot >= 0) g_slot_hits[slot]++;
    g_done++;
    ol_mutex_unlock(&g_mutex);

    free(a);
}

/* ---------- main ---------- */

int main(void) {
    printf("OLSRT Demo 03: Parallel Pool\n");
    printf("=============================\n\n");
    printf("workers      : %d\n", N_WORKERS);
    printf("tasks        : %d\n", N_TASKS);
    printf("work per task: 200000 iterations of i*i\n\n");

    if (ol_mutex_init(&g_mutex) != OL_SUCCESS) {
        fprintf(stderr, "mutex init failed\n");
        return 1;
    }

    ol_parallel_pool_t* pool = ol_parallel_create(N_WORKERS);
    if (!pool) {
        ol_mutex_destroy(&g_mutex);
        fprintf(stderr, "pool create failed\n");
        return 1;
    }

    int64_t t0 = ol_monotonic_now_ns();

    for (int i = 0; i < N_TASKS; i++) {
        task_arg_t* a = (task_arg_t*)malloc(sizeof(task_arg_t));
        if (!a) break;
        a->id = i;
        if (ol_parallel_submit(pool, task_fn, a) != 0) {
            free(a);
            fprintf(stderr, "submit failed at %d\n", i);
            break;
        }
    }

    ol_parallel_flush(pool);
    int64_t t1 = ol_monotonic_now_ns();

    double dt_ms = (double)(t1 - t0) / 1e6;

    printf("completed    : %d / %d\n", g_done, N_TASKS);
    printf("elapsed      : %.2f ms\n", dt_ms);
    printf("avg per task : %.2f ms\n", dt_ms / (double)N_TASKS);
    printf("threads used : %d\n", g_slot_count);
    printf("\nper-thread distribution:\n");
    for (int i = 0; i < g_slot_count; i++) {
        printf("  tid %-10lu : %d tasks\n", g_tids[i], g_slot_hits[i]);
    }

    printf("\nqueue size   : %zu\n", ol_parallel_queue_size(pool));
    printf("is running   : %s\n", ol_parallel_is_running(pool) ? "yes" : "no");

    ol_parallel_destroy(pool);
    ol_mutex_destroy(&g_mutex);

    if (g_done == N_TASKS && g_slot_count == N_WORKERS) {
        printf("\n[OK] all %d tasks completed across %d workers\n",
               N_TASKS, g_slot_count);
        return 0;
    }
    printf("\n[WARN] expected %d workers, saw %d; tasks done = %d\n",
           N_WORKERS, g_slot_count, g_done);
    return 2;
}
