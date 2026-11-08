/* OLSRT side: HTTP server + 3 internal requests. Native. */
#include "bench_util.h"
#include <sys/socket.h>
#include "ol_actor.h"
#include "ol_channel.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"
#include "network/ol_tcp.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT 8190
#define N 3

static ol_event_loop_t *g_loop;
static int g_served = 0;

static int beh(ol_actor_t *a, void *msg) {
    (void)a;
    ol_ask_envelope_t *e = msg;
    if (!e || !e->reply) { if (msg) free(msg); return 0; }
    if (e->payload) free(e->payload);
    const char *b = "Hello from OLSRT demo 16!\n";
    char *resp = malloc(256);
    snprintf(resp, 256,
        "HTTP/1.1 200 OK\r\nServer: OLSRT-bench\r\n"
        "Content-Type: text/plain\r\nContent-Length: %zu\r\n"
        "Connection: close\r\n\r\n%s", strlen(b), b);
    ol_actor_reply_ok(e, resp, free);
    return 0;
}
static void *loop_thread(void *arg) { (void)arg; ol_event_loop_run(g_loop); return NULL; }

static void *client(void *arg) {
    (void)arg;
    struct timespec ts = { 0, 200000000 };
    nanosleep(&ts, NULL);
    for (int i = 0; i < N; i++) {
        ol_tcp_socket_t *c = ol_tcp_socket_create(g_loop);
        ol_tcp_socket_open(c, AF_INET);
        ol_endpoint_t ep; memset(&ep, 0, sizeof(ep));
        strncpy(ep.host, "127.0.0.1", sizeof(ep.host) - 1);
        ep.port = PORT; ep.family = AF_INET;
        int64_t dl = ol_deadline_from_ms(2000).when_ns;
        ol_future_t *cf = ol_tcp_socket_connect(c, &ep, dl);
        if (cf) {
            if (ol_future_await(cf, dl) == 1) (void)ol_future_take_value(cf);
            ol_future_destroy(cf);
            const char *req = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
            ol_future_t *sf = ol_tcp_socket_send(c, req, strlen(req), dl);
            if (sf) { (void)ol_future_await(sf, dl); ol_future_destroy(sf); }
            ol_future_t *rf = ol_tcp_socket_recv(c, 2048, dl);
            if (rf) {
                if (ol_future_await(rf, dl) == 1) {
                    ol_net_buf_t *b = ol_future_take_value(rf);
                    if (b) { if (b->data && b->dtor) b->dtor(b->data); free(b); }
                }
                ol_future_destroy(rf);
            }
        }
        ol_tcp_socket_destroy(c);
        ts.tv_nsec = 100 * 1000000L;
        nanosleep(&ts, NULL);
    }
    return NULL;
}

int main(void) {
    printf("olsrt/16_full_stack: native (TCP + actor + promise)\n");
    printf("%d internal requests\n\n", N);
    uint64_t t0 = bench_now_ns();
    g_loop = ol_event_loop_create();
    ol_actor_t *actor = ol_actor_create(NULL, 16, free, beh, NULL);
    ol_actor_start(actor);
    ol_tcp_socket_t *srv = ol_tcp_socket_create(g_loop);
    ol_tcp_socket_open(srv, AF_INET);
    ol_endpoint_t ep; memset(&ep, 0, sizeof(ep));
    strncpy(ep.host, "127.0.0.1", sizeof(ep.host) - 1);
    ep.port = PORT; ep.family = AF_INET;
    ol_tcp_socket_bind(srv, &ep);
    ol_tcp_socket_listen(srv, 16);
    pthread_t lt, ct;
    pthread_create(&lt, NULL, loop_thread, NULL);
    pthread_create(&ct, NULL, client, NULL);
    while (g_served < N) {
        int64_t dl = ol_deadline_from_ms(5000).when_ns;
        ol_future_t *af = ol_tcp_socket_accept(srv, dl);
        if (!af) break;
        if (ol_future_await(af, dl) != 1) { ol_future_destroy(af); break; }
        ol_tcp_socket_t *cli = ol_future_take_value(af);
        ol_future_destroy(af);
        if (!cli) continue;
        ol_future_t *rf = ol_tcp_socket_recv(cli, 2048, dl);
        if (rf) {
            if (ol_future_await(rf, dl) == 1) {
                ol_net_buf_t *b = ol_future_take_value(rf);
                if (b) {
                    char *raw = malloc(b->len + 1);
                    memcpy(raw, b->data, b->len);
                    raw[b->len] = 0;
                    if (b->data && b->dtor) b->dtor(b->data);
                    free(b);
                    ol_future_t *af2 = ol_actor_ask(actor, raw);
                    if (af2) {
                        if (ol_future_await(af2, dl) == 1) {
                            char *resp = ol_future_take_value(af2);
                            if (resp) {
                                ol_future_t *sf = ol_tcp_socket_send(cli, resp, strlen(resp), dl);
                                if (sf) { (void)ol_future_await(sf, dl); ol_future_destroy(sf); }
                                free(resp);
                            }
                        }
                        ol_future_destroy(af2);
                    }
                }
            }
            ol_future_destroy(rf);
        }
        ol_tcp_socket_destroy(cli);
        g_served++;
    }
    pthread_join(ct, NULL);
    ol_event_loop_stop(g_loop);
    pthread_join(lt, NULL);
    ol_tcp_socket_destroy(srv);
    ol_actor_stop(actor);
    ol_actor_destroy(actor);
    ol_event_loop_destroy(g_loop);
    printf("requests_served:    %d\n", g_served);
    bench_footer(t0, g_served, "requests");
    return 0;
}
