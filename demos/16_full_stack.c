/**
 * @file 16_full_stack.c
 * @brief HTTP -> actor -> promise -> channel, all in one process.
 *
 * A minimal HTTP/1.1 server on 127.0.0.1:8090 serves MAX_REQUESTS
 * connections. A client thread inside the same binary issues those
 * requests, three hundred milliseconds apart, so the accept loop
 * has time to return to accept() between connections.
 *
 * The request path is:
 *
 *     accept (loop thread)
 *       -> handle_client (main thread)
 *         -> recv (loop thread)
 *           -> ask actor (promise)
 *             -> actor logs to channel, replies via promise
 *               -> send (loop thread)
 *                 -> logger thread drains the channel
 *
 * Each step prints a line to stderr so a hang points to the exact
 * call. Set OLSRT_DEMO16_QUIET=1 in the environment to silence it.
 *
 * Port reuse
 * ----------
 * Every accepted socket gets SO_LINGER with a zero timeout. When
 * the socket is closed, the kernel sends RST instead of FIN, which
 * means no TIME_WAIT on port 8090. Without this, running the demo
 * twice in a row fails on the second bind for up to sixty seconds,
 * even with SO_REUSEADDR set.
 */

#include "ol_actor.h"
#include "ol_channel.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_event_loop.h"
#include "ol_promise.h"
#include "network/ol_tcp.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if !defined(_WIN32)
#include <sys/socket.h>
#endif

#define PORT            8090
#define MAX_REQUESTS    3
#define CLIENT_GAP_MS   300

static ol_event_loop_t *g_loop    = NULL;
static ol_channel_t    *g_log_ch  = NULL;
static ol_actor_t      *g_actor   = NULL;
static int              g_verbose = 1;

static void dbg(const char *fmt, ...)
{
    if (!g_verbose)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/*
 * Set SO_LINGER with a zero timeout. Closing the socket sends RST
 * instead of FIN, so no TIME_WAIT is created on the local port.
 */
static void set_linger_zero(ol_tcp_socket_t *s)
{
    if (!s)
        return;
    int fd = ol_tcp_socket_fd(s);
    if (fd < 0)
        return;

#if defined(_WIN32)
    struct linger lg = { 1, 0 };
    (void)setsockopt(fd, SOL_SOCKET, SO_LINGER,
                     (const char *)&lg, (int)sizeof(lg));
#else
    struct linger lg = { 1, 0 };
    (void)setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
#endif
}

static void *logger_thread(void *arg)
{
    (void)arg;
    char *line = NULL;
    while (ol_channel_recv(g_log_ch, (void **)&line) == 1) {
        printf("[log] %s\n", line);
        free(line);
    }
    printf("[log] channel closed\n");
    return NULL;
}

static void *loop_thread(void *arg)
{
    (void)arg;
    ol_event_loop_run(g_loop);
    return NULL;
}

static int handler_beh(ol_actor_t *a, void *msg)
{
    (void)a;
    ol_ask_envelope_t *env = (ol_ask_envelope_t *)msg;
    if (!env || !env->reply) {
        if (msg) free(msg);
        return 0;
    }
    const char *req = (const char *)env->payload;
    if (!req) { ol_actor_reply_error(env, -1); return 0; }

    const char *method = "GET";
    if (strncmp(req, "POST", 4) == 0) method = "POST";
    else if (strncmp(req, "PUT", 3) == 0) method = "PUT";

    char *log_line = (char *)malloc(96);
    if (log_line) {
        snprintf(log_line, 96, "handled %s request (%zu bytes)",
                 method, strlen(req));
        ol_channel_send(g_log_ch, log_line);
    }

    const char *body = "Hello from OLSRT demo 16!\n";
    size_t body_len = strlen(body);
    char *resp = (char *)malloc(256);
    if (!resp) {
        free(env->payload);
        ol_actor_reply_error(env, -1);
        return 0;
    }
    snprintf(resp, 256,
        "HTTP/1.1 200 OK\r\n"
        "Server: OLSRT/1.3.2 (demo 16)\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n%s",
        body_len, body);

    free(env->payload);
    ol_actor_reply_ok(env, resp, free);
    return 0;
}

static void handle_client(ol_tcp_socket_t *cli)
{
    int64_t deadline = ol_deadline_from_ms(2000).when_ns;

    /* Set SO_LINGER as soon as we have the socket. Every return
     * path from this function destroys cli, and we want all of
     * them to leave no TIME_WAIT behind. */
    set_linger_zero(cli);

    dbg("[dbg] handle_client: recv\n");
    ol_future_t *rf = ol_tcp_socket_recv(cli, 2048, deadline);
    if (!rf) { ol_tcp_socket_destroy(cli); return; }
    if (ol_future_await(rf, deadline) != 1) {
        ol_future_destroy(rf);
        ol_tcp_socket_destroy(cli);
        return;
    }
    ol_net_buf_t *nb = (ol_net_buf_t *)ol_future_take_value(rf);
    ol_future_destroy(rf);
    if (!nb || !nb->data || nb->len == 0) {
        if (nb) { if (nb->data && nb->dtor) nb->dtor(nb->data); free(nb); }
        ol_tcp_socket_destroy(cli);
        return;
    }
    char *raw = (char *)malloc(nb->len + 1);
    if (!raw) {
        if (nb->data && nb->dtor) nb->dtor(nb->data);
        free(nb);
        ol_tcp_socket_destroy(cli);
        return;
    }
    memcpy(raw, nb->data, nb->len);
    raw[nb->len] = 0;
    if (nb->data && nb->dtor) nb->dtor(nb->data);
    free(nb);

    dbg("[dbg] handle_client: ask actor\n");
    ol_future_t *af = ol_actor_ask(g_actor, raw);
    if (!af) { free(raw); ol_tcp_socket_destroy(cli); return; }

    if (ol_future_await(af, deadline) == 1) {
        char *resp = (char *)ol_future_take_value(af);
        if (resp) {
            dbg("[dbg] handle_client: send %zu bytes\n", strlen(resp));
            ol_future_t *sf = ol_tcp_socket_send(
                cli, resp, strlen(resp), deadline);
            if (sf) {
                (void)ol_future_await(sf, deadline);
                if (ol_future_state(sf) == OL_PROMISE_FULFILLED) {
                    free(ol_future_take_value(sf));
                }
                ol_future_destroy(sf);
            }
            free(resp);
        }
    }
    ol_future_destroy(af);
    dbg("[dbg] handle_client: destroy cli\n");
    set_linger_zero(cli);
    ol_tcp_socket_destroy(cli);
}

static void *client_thread(void *arg)
{
    (void)arg;
    sleep_ms(200);

    for (int i = 0; i < MAX_REQUESTS; i++) {
        dbg("[dbg] client %d: create\n", i + 1);
        ol_tcp_socket_t *c = ol_tcp_socket_create(g_loop);
        if (!c) break;

        if (ol_tcp_socket_open(c, AF_INET) != 0) {
            ol_tcp_socket_destroy(c);
            break;
        }

        ol_endpoint_t ep;
        memset(&ep, 0, sizeof(ep));
        strncpy(ep.host, "127.0.0.1", sizeof(ep.host) - 1);
        ep.port   = PORT;
        ep.family = AF_INET;

        int64_t deadline = ol_deadline_from_ms(2000).when_ns;

        dbg("[dbg] client %d: connect\n", i + 1);
        ol_future_t *cf = ol_tcp_socket_connect(c, &ep, deadline);
        if (!cf) {
            ol_tcp_socket_destroy(c);
            sleep_ms(CLIENT_GAP_MS);
            continue;
        }
        int cr = ol_future_await(cf, deadline);
        if (cr != 1) {
            ol_future_destroy(cf);
            ol_tcp_socket_destroy(c);
            sleep_ms(CLIENT_GAP_MS);
            continue;
        }
        (void)ol_future_take_value(cf);
        ol_future_destroy(cf);

        const char *req = "GET / HTTP/1.1\r\nHost: localhost\r\n"
                          "Connection: close\r\n\r\n";
        dbg("[dbg] client %d: send request\n", i + 1);
        ol_future_t *sf = ol_tcp_socket_send(c, req, strlen(req), deadline);
        if (sf) {
            (void)ol_future_await(sf, deadline);
            if (ol_future_state(sf) == OL_PROMISE_FULFILLED) {
                free(ol_future_take_value(sf));
            }
            ol_future_destroy(sf);
        }

        dbg("[dbg] client %d: recv response\n", i + 1);
        ol_future_t *rf = ol_tcp_socket_recv(c, 2048, deadline);
        if (rf) {
            if (ol_future_await(rf, deadline) == 1) {
                ol_net_buf_t *nb = (ol_net_buf_t *)ol_future_take_value(rf);
                if (nb) {
                    printf("[client] request %d: %zu bytes\n",
                           i + 1, nb->len);
                    if (nb->data && nb->dtor) nb->dtor(nb->data);
                    free(nb);
                }
            }
            ol_future_destroy(rf);
        }
        dbg("[dbg] client %d: destroy\n", i + 1);
        set_linger_zero(c);
        ol_tcp_socket_destroy(c);

        if (i + 1 < MAX_REQUESTS)
            sleep_ms(CLIENT_GAP_MS);
    }
    return NULL;
}

int main(void)
{
    const char *q = getenv("OLSRT_DEMO16_QUIET");
    g_verbose = (q && *q) ? 0 : 1;

    printf("OLSRT Demo 16: Full Stack\n");
    printf("=========================\n\n");
    printf("flow           : HTTP -> actor -> promise -> channel\n");
    printf("port           : %d\n", PORT);
    printf("requests       : %d\n\n", MAX_REQUESTS);

    int exit_code = 0;

    g_log_ch = ol_channel_create(64, free);
    if (!g_log_ch) return 1;

    g_actor = ol_actor_create(NULL, 32, free, handler_beh, NULL);
    if (!g_actor) {
        ol_channel_destroy(g_log_ch);
        return 1;
    }
    ol_actor_start(g_actor);

    g_loop = ol_event_loop_create();
    if (!g_loop) {
        ol_actor_stop(g_actor);
        ol_actor_destroy(g_actor);
        ol_channel_destroy(g_log_ch);
        return 1;
    }

    pthread_t logger_tid = 0, loop_tid = 0;
    pthread_create(&logger_tid, NULL, logger_thread, NULL);

    ol_tcp_socket_t *srv = ol_tcp_socket_create(g_loop);
    if (!srv) goto cleanup;
    if (ol_tcp_socket_open(srv, AF_INET) != 0) {
        ol_tcp_socket_destroy(srv);
        srv = NULL;
        exit_code = 1;
        goto cleanup;
    }

    ol_endpoint_t ep;
    memset(&ep, 0, sizeof(ep));
    strncpy(ep.host, "127.0.0.1", sizeof(ep.host) - 1);
    ep.port   = PORT;
    ep.family = AF_INET;

    int bound = 0;
    for (int i = 0; i < 20; i++) {
        if (ol_tcp_socket_bind(srv, &ep) == 0) {
            bound = 1;
            break;
        }
        if (i == 0)
            printf("port %d busy, waiting for the previous run ...\n", PORT);
        sleep_ms(100);
    }
    if (!bound || ol_tcp_socket_listen(srv, 8) != 0) {
        fprintf(stderr, "bind/listen failed on port %d\n", PORT);
        ol_tcp_socket_destroy(srv);
        srv = NULL;
        exit_code = 2;
        goto cleanup;
    }
    printf("listening on 127.0.0.1:%d\n", PORT);

    pthread_create(&loop_tid, NULL, loop_thread, NULL);

    pthread_t client_tid;
    pthread_create(&client_tid, NULL, client_thread, NULL);

    int served = 0;
    while (served < MAX_REQUESTS) {
        int64_t      dl = ol_deadline_from_ms(5000).when_ns;
        ol_future_t *af = ol_tcp_socket_accept(srv, dl);
        if (!af) continue;
        if (ol_future_await(af, dl) != 1) {
            ol_future_destroy(af);
            break;
        }
        ol_tcp_socket_t *cli = (ol_tcp_socket_t *)ol_future_take_value(af);
        ol_future_destroy(af);
        if (!cli) continue;

        /* Set SO_LINGER immediately, so even a failed handle_client
         * leaves no TIME_WAIT behind. */
        set_linger_zero(cli);
        dbg("[dbg] main: accepted connection %d\n", served + 1);
        handle_client(cli);
        served++;
    }

    pthread_join(client_tid, NULL);
    ol_tcp_socket_destroy(srv);
    srv = NULL;

cleanup:
    if (g_loop)
        ol_event_loop_stop(g_loop);
    if (loop_tid)
        pthread_join(loop_tid, NULL);
    if (g_actor) {
        ol_actor_stop(g_actor);
        ol_actor_destroy(g_actor);
        g_actor = NULL;
    }
    if (g_log_ch) {
        ol_channel_close(g_log_ch);
        if (logger_tid)
            pthread_join(logger_tid, NULL);
        ol_channel_destroy(g_log_ch);
        g_log_ch = NULL;
    }
    if (g_loop) {
        ol_event_loop_destroy(g_loop);
        g_loop = NULL;
    }

    if (exit_code == 0)
        printf("\n[OK] full stack demo completed\n");
    else
        printf("\n[FAIL] demo did not complete (exit code %d)\n", exit_code);

    return exit_code;
}
