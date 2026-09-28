/**
 * @file 07_http_server.c
 * @brief Minimal HTTP server on OLSRT TCP + event loop.
 *
 * Listens on 0.0.0.0:8080, serves 3 requests, then exits.
 * Test:  ./07_test.sh   (runs 3 curl requests)
 */

#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"
#include "ol_lock_mutex.h"
#include "ol_promise.h"
#include "network/ol_tcp.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define PORT      8080
#define MAX_REQ   3

static ol_event_loop_t* g_loop = NULL;

static void* loop_thread(void* arg) {
    (void)arg;
    ol_event_loop_run(g_loop);
    return NULL;
}

/* Build an HTTP response into buf. Returns total length. */
static int make_response(char* buf, size_t cap) {
    const char* body = "Hello from OLSRT!\n";
    int n = snprintf(buf, cap,
        "HTTP/1.1 200 OK\r\n"
        "Server: OLSRT/1.3.1\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        strlen(body), body);
    return n < 0 ? 0 : n;
}

int main(void) {
    printf("OLSRT Demo 07: HTTP Server\n");
    printf("===========================\n\n");

    g_loop = ol_event_loop_create();
    if (!g_loop) return 1;

    ol_tcp_socket_t* srv = ol_tcp_socket_create(g_loop);
    if (!srv) { ol_event_loop_destroy(g_loop); return 1; }

    if (ol_tcp_socket_open(srv, AF_INET) != 0) {
        ol_tcp_socket_destroy(srv);
        ol_event_loop_destroy(g_loop);
        return 1;
    }

    ol_endpoint_t ep;
    memset(&ep, 0, sizeof(ep));
    strncpy(ep.host, "0.0.0.0", sizeof(ep.host) - 1);
    ep.port   = PORT;
    ep.family = AF_INET;

    if (ol_tcp_socket_bind(srv, &ep) != 0 ||
        ol_tcp_socket_listen(srv, 16) != 0) {
        fprintf(stderr, "bind/listen failed on port %d\n", PORT);
        ol_tcp_socket_destroy(srv);
        ol_event_loop_destroy(g_loop);
        return 1;
    }

    printf("listening on 0.0.0.0:%d, will serve %d request(s)\n", PORT, MAX_REQ);
    printf("run ./07_test.sh from another terminal, or:\n");
    printf("  curl -s http://127.0.0.1:%d/\n\n", PORT);

    pthread_t th;
    if (pthread_create(&th, NULL, loop_thread, NULL) != 0) {
        ol_tcp_socket_destroy(srv);
        ol_event_loop_destroy(g_loop);
        return 1;
    }

    int served = 0;
    while (served < MAX_REQ) {
        int64_t dl = ol_deadline_from_ms(20000).when_ns;

        ol_future_t* af = ol_tcp_socket_accept(srv, dl);
        if (!af) break;
        if (ol_future_await(af, dl) != 1) { ol_future_destroy(af); break; }
        ol_tcp_socket_t* cli = (ol_tcp_socket_t*)ol_future_take_value(af);
        ol_future_destroy(af);
        printf("[%d] client connected\n", served + 1);

        ol_future_t* rf = ol_tcp_socket_recv(cli, 8192, dl);
        if (rf) {
            if (ol_future_await(rf, dl) == 1) {
                ol_net_buf_t* b = (ol_net_buf_t*)ol_future_take_value(rf);
                if (b) {
                    printf("[%d] got %zu bytes\n", served + 1, b->len);
                    if (b->data && b->dtor) b->dtor(b->data);
                    free(b);
                }
            }
            ol_future_destroy(rf);
        }

        char resp[512];
        int n = make_response(resp, sizeof(resp));
        ol_future_t* sf = ol_tcp_socket_send(cli, resp, (size_t)n, dl);
        if (sf) {
            if (ol_future_await(sf, dl) == 1) {
                void* v = ol_future_take_value(sf);
                free(v);
            }
            ol_future_destroy(sf);
        }

        ol_tcp_socket_destroy(cli);
        served++;
    }

    ol_event_loop_stop(g_loop);
    pthread_join(th, NULL);
    ol_tcp_socket_destroy(srv);
    ol_event_loop_destroy(g_loop);

    printf("\n[server] served %d request(s)\n", served);
    return served == MAX_REQ ? 0 : 1;
}
