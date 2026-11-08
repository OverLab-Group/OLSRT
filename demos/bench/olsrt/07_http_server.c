/* OLSRT side: TCP HTTP server, exits after MAX_REQ. Native (network/ol_tcp). */
#include "bench_util.h"
#include "ol_promise.h"
#include <sys/socket.h>
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"
#include "network/ol_tcp.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT 8180
#define MAX_REQ 100

static ol_event_loop_t *g_loop;
static int g_served = 0;

static void *loop_thread(void *arg) { (void)arg; ol_event_loop_run(g_loop); return NULL; }

int main(void) {
    printf("olsrt/07_http_server: native (ol_tcp)\n");
    printf("port %d, will serve %d requests then exit\n\n", PORT, MAX_REQ);
    uint64_t t0 = bench_now_ns();
    g_loop = ol_event_loop_create();
    ol_tcp_socket_t *srv = ol_tcp_socket_create(g_loop);
    ol_tcp_socket_open(srv, AF_INET);
    ol_endpoint_t ep; memset(&ep, 0, sizeof(ep));
    strncpy(ep.host, "127.0.0.1", sizeof(ep.host) - 1);
    ep.port = PORT; ep.family = AF_INET;
    ol_tcp_socket_bind(srv, &ep);
    ol_tcp_socket_listen(srv, 32);
    pthread_t th;
    pthread_create(&th, NULL, loop_thread, NULL);

    const char *body = "Hello from OLSRT!\n";
    char resp[256];
    int rlen = snprintf(resp, sizeof(resp),
        "HTTP/1.1 200 OK\r\nServer: OLSRT-bench\r\n"
        "Content-Type: text/plain\r\nContent-Length: %zu\r\n"
        "Connection: close\r\n\r\n%s", strlen(body), body);

    while (g_served < MAX_REQ) {
        int64_t dl = ol_deadline_from_ms(10000).when_ns;
        ol_future_t *af = ol_tcp_socket_accept(srv, dl);
        if (!af) continue;
        if (ol_future_await(af, dl) != 1) { ol_future_destroy(af); break; }
        ol_tcp_socket_t *cli = ol_future_take_value(af);
        ol_future_destroy(af);
        if (!cli) continue;
        ol_future_t *rf = ol_tcp_socket_recv(cli, 4096, dl);
        if (rf) {
            if (ol_future_await(rf, dl) == 1) {
                ol_net_buf_t *b = ol_future_take_value(rf);
                if (b) { if (b->data && b->dtor) b->dtor(b->data); free(b); }
            }
            ol_future_destroy(rf);
        }
        ol_future_t *sf = ol_tcp_socket_send(cli, resp, (size_t)rlen, dl);
        if (sf) {
            if (ol_future_await(sf, dl) == 1) {
                void *v = ol_future_take_value(sf);
                free(v);
            }
            ol_future_destroy(sf);
        }
        ol_tcp_socket_destroy(cli);
        g_served++;
    }
    ol_event_loop_stop(g_loop);
    pthread_join(th, NULL);
    ol_tcp_socket_destroy(srv);
    ol_event_loop_destroy(g_loop);
    printf("requests_served:    %d\n", g_served);
    bench_footer(t0, g_served, "requests");
    return 0;
}
