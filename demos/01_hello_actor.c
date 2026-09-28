/**
 * @file 01_hello_actor.c
 * @brief First OLSRT demo: an uppercase echo actor.
 *
 * Creates an actor that uppercases incoming strings. Uses ol_actor_ask
 * to send requests and reads replies via futures.
 *
 * The process-based actor loop is not yet driven by the green-thread
 * scheduler (Wave 2 work). This demo pumps the mailbox manually via
 * ol_actor_process_batch(), which is part of the public API.
 */

#include "ol_actor.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_promise.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Actor behavior                                                     */
/* ------------------------------------------------------------------ */

static int uppercase_beh(ol_actor_t* a, void* msg) {
    (void)a;

    ol_ask_envelope_t* env = (ol_ask_envelope_t*)msg;
    if (!env || !env->reply) {
        if (msg) free(msg);
        return 0;
    }

    char* in = (char*)env->payload;
    if (!in) {
        ol_actor_reply_error(env, -1);
        return 0;
    }

    for (char* p = in; *p; ++p) {
        *p = (char)toupper((unsigned char)*p);
    }

    /* Ownership of `in` transfers to the promise; freed via `free`
     * when the promise is destroyed. */
    ol_actor_reply_ok(env, in, free);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Helper: pump the actor until the future resolves                   */
/* ------------------------------------------------------------------ */

static int pump_until_resolved(ol_actor_t* actor, ol_future_t* f,
                               int64_t deadline_ns) {
    while (1) {
        if (ol_future_state(f) != OL_PROMISE_PENDING) return 1;
        if (ol_monotonic_now_ns() >= deadline_ns) return 0;

        size_t n = ol_actor_process_batch(actor, 16);
        if (n == 0) {
            struct timespec ts = { 0, 1000000 };  /* 1 ms */
            nanosleep(&ts, NULL);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("OLSRT Demo 01: Hello Actor\n");
    printf("==========================\n\n");

    ol_actor_t* echo = ol_actor_create(NULL, 64, free, uppercase_beh, NULL);
    if (!echo) {
        fprintf(stderr, "ol_actor_create failed\n");
        return 1;
    }
    printf("[main] actor created  (mailbox capacity = %zu)\n",
           ol_actor_mailbox_capacity(echo));

    ol_actor_start(echo);

    const char* words[] = { "hello", "world", "olsrt", "actor", "model" };
    const int n = (int)(sizeof(words) / sizeof(words[0]));

    for (int i = 0; i < n; i++) {
        char* payload = strdup(words[i]);
        if (!payload) break;

        printf("[main] ask  ->  '%s'\n", payload);

        int64_t t0 = ol_monotonic_now_ns();
        ol_future_t* f = ol_actor_ask(echo, payload);
        if (!f) {
            fprintf(stderr, "[main] ask failed\n");
            free(payload);
            continue;
        }

        int64_t deadline = ol_deadline_from_ms(1000).when_ns;
        if (!pump_until_resolved(echo, f, deadline)) {
            fprintf(stderr, "[main] timeout waiting for reply\n");
            ol_future_destroy(f);
            continue;
        }

        int64_t dt_us = (ol_monotonic_now_ns() - t0) / 1000;
        ol_promise_state_t st = ol_future_state(f);

        if (st == OL_PROMISE_FULFILLED) {
            const char* r = (const char*)ol_future_get_value_const(f);
            printf("[main] reply <-  '%s'   (%lld us)\n",
                   r ? r : "(null)", (long long)dt_us);
        } else if (st == OL_PROMISE_REJECTED) {
            printf("[main] rejected with code %d\n", ol_future_error(f));
        } else {
            printf("[main] canceled\n");
        }

        ol_future_destroy(f);
    }

    ol_actor_stats_t stats;
    if (ol_actor_get_stats(echo, &stats) == 0) {
        printf("\n[main] actor stats\n");
        printf("       messages processed : %llu\n",
               (unsigned long long)stats.processed_messages);
        printf("       total time (ns)    : %llu\n",
               (unsigned long long)stats.processing_time_ns);
        printf("       peak mailbox size  : %zu\n", stats.mailbox_peak);
    }

    ol_actor_stop(echo);
    ol_actor_destroy(echo);

    printf("\n[main] done\n");
    return 0;
}
