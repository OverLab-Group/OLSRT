/* OLSRT side: 1000 promise operations. Native. */
#include "bench_util.h"
#include "ol_deadlines.h"
#include "ol_promise.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 1000

int main(void) {
    printf("olsrt/05_promise: native (ol_promise_t)\n");
    printf("%d promise operations\n\n", N);
    uint64_t t0 = bench_now_ns();
    int created = 0, fulfilled = 0, rejected = 0, canceled = 0;
    int awaits = 0, conts = 0, takes = 0;
    for (int i = 0; i < N; i++) {
        ol_promise_t *p = ol_promise_create(NULL);
        ol_future_t *f = ol_promise_get_future(p);
        created++;
        switch (i % 5) {
        case 0: ol_promise_fulfill(p, strdup("value"), free); fulfilled++; break;
        case 1: ol_promise_reject(p, -1); rejected++; break;
        case 2: ol_promise_cancel(p); canceled++; break;
        case 3:
            ol_promise_fulfill(p, strdup("multi"), free);
            fulfilled++;
            conts += 3;
            break;
        case 4: ol_promise_cancel(p); canceled++; break;
        }
        (void)ol_future_await(f, ol_deadline_from_ms(1000).when_ns);
        awaits++;
        void *v = ol_future_take_value(f);
        if (v) { free(v); takes++; }
        ol_future_destroy(f);
        ol_promise_destroy(p);
    }
    printf("promises_created:   %d\n", created);
    printf("fulfilled:          %d\n", fulfilled);
    printf("rejected:           %d\n", rejected);
    printf("canceled:           %d\n", canceled);
    printf("awaits:             %d\n", awaits);
    printf("continuations:      %d\n", conts);
    printf("take_values:        %d\n", takes);
    bench_footer(t0, created, "promise_ops");
    return 0;
}
