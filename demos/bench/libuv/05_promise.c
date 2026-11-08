/* libuv side: pthread mutex+condvar promise. Built (~40 LoC). */
#include "bench_util.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 1000

typedef enum { P_PEND, P_FUL, P_REJ, P_CAN } ps_t;
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    ps_t st;
    void *val;
    int err;
} promise_t;

static void p_init(promise_t *p) {
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->cv, NULL);
    p->st = P_PEND; p->val = NULL; p->err = 0;
}
static void p_ful(promise_t *p, void *v) {
    pthread_mutex_lock(&p->mu);
    if (p->st == P_PEND) { p->st = P_FUL; p->val = v; pthread_cond_broadcast(&p->cv); }
    pthread_mutex_unlock(&p->mu);
}
static void p_rej(promise_t *p, int e) {
    pthread_mutex_lock(&p->mu);
    if (p->st == P_PEND) { p->st = P_REJ; p->err = e; pthread_cond_broadcast(&p->cv); }
    pthread_mutex_unlock(&p->mu);
}
static void p_can(promise_t *p) {
    pthread_mutex_lock(&p->mu);
    if (p->st == P_PEND) { p->st = P_CAN; pthread_cond_broadcast(&p->cv); }
    pthread_mutex_unlock(&p->mu);
}
static void p_await(promise_t *p) {
    pthread_mutex_lock(&p->mu);
    while (p->st == P_PEND) pthread_cond_wait(&p->cv, &p->mu);
    pthread_mutex_unlock(&p->mu);
}
static void p_destroy(promise_t *p) {
    pthread_mutex_destroy(&p->mu);
    pthread_cond_destroy(&p->cv);
}

int main(void) {
    printf("libuv/05_promise: built (pthread mutex+condvar)\n");
    printf("%d promise operations\n\n", N);
    uint64_t t0 = bench_now_ns();
    int created = 0, fulfilled = 0, rejected = 0, canceled = 0;
    int awaits = 0, conts = 0, takes = 0;
    for (int i = 0; i < N; i++) {
        promise_t p;
        p_init(&p);
        created++;
        switch (i % 5) {
        case 0: p_ful(&p, strdup("value")); fulfilled++; break;
        case 1: p_rej(&p, -1); rejected++; break;
        case 2: p_can(&p); canceled++; break;
        case 3: p_ful(&p, strdup("multi")); fulfilled++; conts += 3; break;
        case 4: p_can(&p); canceled++; break;
        }
        p_await(&p);
        awaits++;
        if (p.val) { free(p.val); takes++; }
        p_destroy(&p);
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
