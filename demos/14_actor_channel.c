/**
 * @file 14_actor_channel.c
 * @brief Channel-driven actor pipeline.
 *
 * A producer thread sends items into a channel. An actor drains the
 * channel and squares each item. The channel is the only
 * synchronization primitive between the two sides.
 */

#include "ol_actor.h"
#include "ol_channel.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_parallel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N_ITEMS 8

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void producer_fn(void *arg)
{
    ol_channel_t *ch = (ol_channel_t *)arg;
    for (int i = 1; i <= N_ITEMS; i++) {
        int *v = (int *)malloc(sizeof(int));
        if (!v) break;
        *v = i;
        if (ol_channel_send(ch, v) != 0) { free(v); break; }
        printf("[producer] sent %d\n", i);
        sleep_ms(20);
    }
    ol_channel_close(ch);
    printf("[producer] channel closed\n");
}

static int consumer_beh(ol_actor_t *a, void *msg)
{
    (void)msg;
    ol_channel_t *ch = (ol_channel_t *)ol_actor_get_context(a);
    if (!ch) return 0;

    printf("[consumer] draining channel\n");
    void *item = NULL;
    int count = 0;
    while (ol_channel_recv(ch, &item) == 1) {
        int v = *(int *)item;
        free(item);
        printf("[consumer] got %d -> squared %d\n", v, v * v);
        count++;
    }
    printf("[consumer] drained, %d items processed\n", count);
    return 1;
}

int main(void)
{
    printf("OLSRT Demo 14: Channel-driven Actor Pipeline\n");
    printf("=============================================\n\n");
    printf("items          : %d\n", N_ITEMS);
    printf("topology       : producer -> channel -> actor\n\n");

    ol_channel_t *ch = ol_channel_create(0, free);
    if (!ch) return 1;

    ol_parallel_pool_t *pool = ol_parallel_create(2);
    if (!pool) { ol_channel_destroy(ch); return 1; }

    ol_actor_t *consumer = ol_actor_create(NULL, 16, free,
                                            consumer_beh, ch);
    if (!consumer) {
        ol_parallel_destroy(pool); ol_channel_destroy(ch); return 1;
    }
    ol_actor_start(consumer);

    if (ol_parallel_submit(pool, producer_fn, ch) != 0) {
        ol_actor_stop(consumer);
        ol_actor_destroy(consumer);
        ol_parallel_destroy(pool);
        ol_channel_destroy(ch);
        return 1;
    }

    /* Kick the actor once so its behavior runs and blocks on recv. */
    ol_actor_send(consumer, strdup("go"));

    ol_parallel_flush(pool);
    sleep_ms(200);

    ol_actor_stop(consumer);
    ol_actor_destroy(consumer);
    ol_parallel_destroy(pool);
    ol_channel_destroy(ch);

    printf("\n[OK] pipeline completed\n");
    return 0;
}
