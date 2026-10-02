/**
 * @file 08_coroutines.c
 * @brief Cooperative producer/consumer on a coroutine scheduler.
 *
 * @details
 * Two coroutines run on the same OS thread. The producer yields
 * one item at a time; the consumer yields the running total. The
 * scheduler interleaves them cooperatively.
 *
 * This demo also introduces the ORoutine scheduler primitives that
 * v1.3.4 will build on.
 */

#include "ol_common.h"
#include "ol_coroutines.h"

#include <stdio.h>
#include <stdlib.h>

#define N_ITEMS 8

static int g_items[N_ITEMS] = { 3, 1, 4, 1, 5, 9, 2, 6 };

static void *producer(void *arg) {
    (void)arg;
    for (int i = 0; i < N_ITEMS; i++) {
        int *item = (int*)malloc(sizeof(int));
        *item = g_items[i];
        printf("[producer] yield %d\n", *item);
        void *resume = ol_co_yield(item);
        free(resume);
    }
    return NULL;
}

static void *consumer(void *arg) {
    (void)arg;
    long sum = 0;
    for (int i = 0; i < N_ITEMS; i++) {
        int *item = (int*)ol_co_yield(NULL);
        if (!item) continue;
        sum += *item;
        printf("[consumer] got %d, running sum = %ld\n", *item, sum);
        free(item);
    }
    long *out = (long*)malloc(sizeof(long));
    *out = sum;
    return out;
}

int main(void) {
    printf("OLSRT Demo 08: Coroutines\n");
    printf("=========================\n\n");

    if (ol_coroutine_scheduler_init() != 0) {
        fprintf(stderr, "scheduler init failed\n");
        return 1;
    }

    ol_co_t *p = ol_co_spawn(producer, NULL, 64 * 1024);
    ol_co_t *c = ol_co_spawn(consumer, NULL, 64 * 1024);
    if (!p || !c) {
        fprintf(stderr, "spawn failed\n");
        ol_coroutine_scheduler_shutdown();
        return 1;
    }

    for (int i = 0; i < N_ITEMS; i++) {
        (void)ol_co_resume(p, NULL);
        (void)ol_co_resume(c, NULL);
    }

    (void)ol_co_join(c);
    long *result = (long*)ol_co_join(c);
    if (result) {
        printf("\n[main] consumer total = %ld\n", *result);
        free(result);
    }

    ol_co_destroy(p);
    ol_co_destroy(c);
    ol_coroutine_scheduler_shutdown();

    printf("\n[OK] coroutines completed\n");
    return 0;
}
