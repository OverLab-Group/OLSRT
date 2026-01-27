/**
 * @file test_race_conditions.c
 * @brief Race condition tests for the concurrency library
 */

#include "ol_green_threads.h"
#include "ol_lock_mutex.h"
#include "ol_parallel.h"
#include "ol_channel.h"
#include "ol_promise.h"
#include "ol_actor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEST_ASSERT(cond, msg) \
do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s at %s:%d\n", msg, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

/* Test 1: Race condition in parallel pool */
static volatile int test1_counter = 0;
static ol_mutex_t test1_mutex;

static void test1_task(void *arg) {
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        ol_mutex_lock(&test1_mutex);
        int val = test1_counter;
        /* Simulate some work */
        for (volatile int j = 0; j < 100; j++);
        test1_counter = val + 1;
        ol_mutex_unlock(&test1_mutex);
    }
}

static void test_race_parallel_pool(void) {
    printf("Test 1: Race condition in parallel pool...\n");

    ol_mutex_init(&test1_mutex);
    test1_counter = 0;

    ol_parallel_pool_t *pool = ol_parallel_create(4);
    TEST_ASSERT(pool != NULL, "Failed to create pool");

    /* Submit many tasks */
    for (int i = 0; i < 100; i++) {
        int r = ol_parallel_submit(pool, test1_task, NULL);
        TEST_ASSERT(r == 0, "Failed to submit task");
    }

    /* Wait for completion */
    int r = ol_parallel_flush(pool);
    TEST_ASSERT(r == 0, "Failed to flush pool");

    /* Check counter */
    TEST_ASSERT(test1_counter == 100000, "Race condition detected");
    printf("  Counter = %d (expected 100000)\n", test1_counter);

    ol_parallel_destroy(pool);
    ol_mutex_destroy(&test1_mutex);
    printf("  PASS\n");
}

/* Test 2: Channel race condition */
static void test_race_channel(void) {
    printf("Test 2: Race condition in channels...\n");

    ol_channel_t *ch = ol_channel_create(10, NULL);
    TEST_ASSERT(ch != NULL, "Failed to create channel");

    volatile int send_count = 0;
    volatile int recv_count = 0;

    /* Create sender and receiver tasks */
    ol_parallel_pool_t *pool = ol_parallel_create(2);

    /* Sender task */
    ol_parallel_submit(pool, [](void *arg) {
        ol_channel_t *ch = (ol_channel_t*)arg;
        for (int i = 0; i < 10000; i++) {
            int *val = (int*)malloc(sizeof(int));
            *val = i;
            while (ol_channel_try_send(ch, val) == 0) {
                /* Busy wait or yield */
                sched_yield();
            }
            __sync_fetch_and_add((int*)&send_count, 1);
        }
    }, ch);

    /* Receiver task */
    ol_parallel_submit(pool, [](void *arg) {
        ol_channel_t *ch = (ol_channel_t*)arg;
        for (int i = 0; i < 10000; i++) {
            void *val = NULL;
            while (ol_channel_try_recv(ch, &val) == 0) {
                sched_yield();
            }
            if (val) {
                free(val);
                __sync_fetch_and_add((int*)&recv_count, 1);
            }
        }
    }, ch);

    ol_parallel_flush(pool);
    ol_parallel_destroy(pool);

    TEST_ASSERT(send_count == 10000, "Send count mismatch");
    TEST_ASSERT(recv_count == 10000, "Receive count mismatch");
    TEST_ASSERT(ol_channel_len(ch) == 0, "Channel not empty");

    ol_channel_destroy(ch);
    printf("  PASS\n");
}

/* Test 3: Promise race condition */
static void test_race_promise(void) {
    printf("Test 3: Race condition in promises...\n");

    ol_promise_t *promises[100];
    ol_future_t *futures[100];

    /* Create promises */
    for (int i = 0; i < 100; i++) {
        promises[i] = ol_promise_create(NULL);
        TEST_ASSERT(promises[i] != NULL, "Failed to create promise");
        futures[i] = ol_promise_get_future(promises[i]);
        TEST_ASSERT(futures[i] != NULL, "Failed to get future");
    }

    /* Multiple threads fulfilling promises */
    ol_parallel_pool_t *pool = ol_parallel_create(4);

    for (int i = 0; i < 100; i++) {
        int *index = (int*)malloc(sizeof(int));
        *index = i;

        ol_parallel_submit(pool, [](void *arg) {
            int idx = *(int*)arg;

            /* Random delay to increase chance of race */
            struct timespec ts = {0, (rand() % 1000) * 1000};
            nanosleep(&ts, NULL);

            int *val = (int*)malloc(sizeof(int));
            *val = idx * 2;

            ol_promise_fulfill(promises[idx], val, free);
            free(arg);
        }, index);
    }

    /* Wait for all promises */
    for (int i = 0; i < 100; i++) {
        int r = ol_future_await(futures[i], 1000000000); /* 1 second timeout */
        TEST_ASSERT(r == 1, "Future await failed");

        const int *val = (const int*)ol_future_get_value_const(futures[i]);
        TEST_ASSERT(val != NULL, "Future value is NULL");
        TEST_ASSERT(*val == i * 2, "Incorrect future value");
    }

    ol_parallel_flush(pool);
    ol_parallel_destroy(pool);

    /* Cleanup */
    for (int i = 0; i < 100; i++) {
        ol_future_destroy(futures[i]);
        ol_promise_destroy(promises[i]);
    }

    printf("  PASS\n");
}

/* Test 4: Actor mailbox race condition */
static volatile int actor_counter = 0;

static int test_actor_behavior(ol_actor_t *actor, void *msg) {
    (void)actor;
    if (msg) {
        int *val = (int*)msg;
        __sync_fetch_and_add(&actor_counter, *val);
        free(val);
    }
    return 0;
}

static void test_race_actor(void) {
    printf("Test 4: Race condition in actors...\n");

    ol_parallel_pool_t *pool = ol_parallel_create(4);
    TEST_ASSERT(pool != NULL, "Failed to create pool");

    ol_actor_t *actor = ol_actor_create(pool, 1000, free, test_actor_behavior, NULL);
    TEST_ASSERT(actor != NULL, "Failed to create actor");

    int r = ol_actor_start(actor);
    TEST_ASSERT(r == 0, "Failed to start actor");

    /* Send many messages from multiple threads */
    for (int i = 0; i < 1000; i++) {
        int *val = (int*)malloc(sizeof(int));
        *val = 1;

        ol_parallel_submit(pool, [](void *arg) {
            ol_actor_t *actor = (ol_actor_t*)arg;
            for (int j = 0; j < 100; j++) {
                int *val = (int*)malloc(sizeof(int));
                *val = 1;
                ol_actor_send(actor, val);
            }
        }, actor);
    }

    /* Wait for messages to be processed */
    while (ol_actor_mailbox_len(actor) > 0) {
        sched_yield();
    }

    /* Give actor time to process */
    struct timespec ts = {0, 100000000}; /* 100ms */
    nanosleep(&ts, NULL);

    /* Stop actor */
    ol_actor_stop(actor);

    TEST_ASSERT(actor_counter == 100000, "Actor counter mismatch");
    printf("  Actor counter = %d (expected 100000)\n", actor_counter);

    ol_actor_destroy(actor);
    ol_parallel_destroy(pool);

    printf("  PASS\n");
}

/* Test 5: Green threads race condition */
static volatile int gt_counter = 0;
static ol_mutex_t gt_mutex;

static void gt_race_task(void *arg) {
    (void)arg;
    for (int i = 0; i < 100; i++) {
        ol_mutex_lock(&gt_mutex);
        int val = gt_counter;
        ol_gt_yield(); /* Yield while holding mutex - stress test */
        gt_counter = val + 1;
        ol_mutex_unlock(&gt_mutex);
        ol_gt_yield();
    }
}

static void test_race_green_threads(void) {
    printf("Test 5: Race condition in green threads...\n");

    ol_mutex_init(&gt_mutex);
    gt_counter = 0;

    ol_gt_scheduler_init();

    /* Spawn many green threads */
    ol_gt_t *threads[10];
    for (int i = 0; i < 10; i++) {
        threads[i] = ol_gt_spawn(gt_race_task, NULL, 64 * 1024);
        TEST_ASSERT(threads[i] != NULL, "Failed to spawn green thread");
    }

    /* Run until all complete */
    for (int i = 0; i < 10; i++) {
        while (ol_gt_is_alive(threads[i])) {
            ol_gt_resume(threads[i]);
        }
    }

    TEST_ASSERT(gt_counter == 1000, "Green thread counter mismatch");
    printf("  Green thread counter = %d (expected 1000)\n", gt_counter);

    /* Cleanup */
    for (int i = 0; i < 10; i++) {
        ol_gt_destroy(threads[i]);
    }

    ol_gt_scheduler_shutdown();
    ol_mutex_destroy(&gt_mutex);

    printf("  PASS\n");
}

/* Main test runner */
int main(void) {
    printf("=== Race Condition Tests ===\n");

    /* Seed random number generator */
    srand((unsigned int)time(NULL));

    /* Run tests */
    test_race_parallel_pool();
    test_race_channel();
    test_race_promise();
    test_race_actor();
    test_race_green_threads();

    printf("\n=== All Tests PASSED ===\n");
    return 0;
}
