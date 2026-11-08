/* OLSRT side: 1000 ask/reply round-trips. Native actor primitive. */
#include "bench_util.h"
#include "ol_actor.h"
#include "ol_deadlines.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define N 1000

static int beh(ol_actor_t *a, void *msg) {
    (void)a;
    ol_ask_envelope_t *e = msg;
    if (!e || !e->reply) { if (msg) free(msg); return 0; }
    char *s = e->payload;
    for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p);
    ol_actor_reply_ok(e, s, free);
    return 0;
}

int main(void) {
    printf("olsrt/01_hello_actor: native (actor + ask/reply)\n");
    printf("%d round-trips\n\n", N);
    uint64_t t0 = bench_now_ns();
    ol_actor_t *a = ol_actor_create(NULL, 64, free, beh, NULL);
    ol_actor_start(a);
    const char *w[5] = { "hello", "world", "olsrt", "actor", "model" };
    uint64_t sum = 0, mn = UINT64_MAX, mx = 0;
    int ok = 0;
    for (int i = 0; i < N; i++) {
        uint64_t s = bench_now_ns();
        ol_future_t *f = ol_actor_ask(a, strdup(w[i % 5]));
        if (ol_future_await(f, ol_deadline_from_ms(2000).when_ns) == 1) {
            uint64_t d = bench_now_ns() - s;
            sum += d; if (d < mn) mn = d; if (d > mx) mx = d;
            ok++;
        }
        ol_future_destroy(f);
    }
    ol_actor_stop(a);
    ol_actor_destroy(a);
    printf("requests_sent:      %d\n", N);
    printf("replies_received:   %d\n", ok);
    printf("rtt_mean_us:        %.2f\n", ok ? (double)sum / ok / 1000.0 : 0.0);
    printf("rtt_min_us:         %.2f\n", mn == UINT64_MAX ? 0 : (double)mn / 1000.0);
    printf("rtt_max_us:         %.2f\n", (double)mx / 1000.0);
    bench_footer(t0, N, "round_trips");
    return 0;
}
