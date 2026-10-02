/**
 * @file main.c
 * @brief gold_ws — HTTP/1.1 web server on OLSRT.
 *
 * @details
 * The accept loop runs on the main thread and hands each accepted
 * client off to a worker thread from the OLSRT parallel pool. The
 * worker reads the request, creates a dedicated actor via
 * ol_actor_create, and asks that actor for a response. The actor
 * parses the request, dispatches on the URL, and replies via
 * promise. The worker writes the response back and closes the
 * connection.
 *
 * v1.3.2 fixes applied:
 *   1. The event loop runs on its own thread.
 *   2. Responses carry an explicit length through the promise.
 *   3. script_root is resolved to an absolute path at startup.
 *   4. In handle_client, the client socket is closed BEFORE the
 *      actor is destroyed. ol_actor_destroy() joins the driver
 *      thread and frees two arenas, which on this machine takes
 *      15-20 ms. If the socket stays open during that window, the
 *      client's "wait for FIN" measurement includes the cleanup
 *      time, which made ab report ~22 ms per request while curl
 *      (which stops at Content-Length) reported ~3.5 ms. Reordering
 *      the two calls removes that difference.
 */

#include "ol_actor.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_parallel.h"
#include "ol_promise.h"
#include "network/ol_tcp.h"

#include "http.h"
#include "router.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PORT        8080
#define DEFAULT_FPM_HOST    "127.0.0.1"
#define DEFAULT_FPM_PORT    9000
#define DEFAULT_WORKERS     4
#define DEFAULT_ROOT        "public"
#define DEFAULT_SCRIPT_ROOT "public"

typedef struct {
    int              port;
    const char      *root;
    const char      *script_root;
    const char      *fpm_host;
    int              fpm_port;
    int              workers;
    ol_event_loop_t *loop;
} config_t;

static config_t            g_cfg;
static ol_parallel_pool_t *g_pool;

static char g_root_buf[PATH_MAX];
static char g_script_root_buf[PATH_MAX];

static const char *make_abs_path(const char *rel, char *buf, size_t buf_size)
{
    if (!rel || !*rel || buf_size == 0)
        return rel;
    if (rel[0] == '/') {
        strncpy(buf, rel, buf_size - 1);
        buf[buf_size - 1] = 0;
        return buf;
    }
    char resolved[PATH_MAX];
    if (realpath(rel, resolved)) {
        strncpy(buf, resolved, buf_size - 1);
        buf[buf_size - 1] = 0;
        return buf;
    }
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        snprintf(buf, buf_size, "%s/%s", cwd, rel);
        return buf;
    }
    return rel;
}

typedef struct {
    char  *data;
    size_t len;
} http_response_t;

static void http_response_destroy(void *p)
{
    http_response_t *r = (http_response_t *)p;
    if (!r) return;
    free(r->data);
    free(r);
}

static void *loop_thread(void *arg)
{
    (void)arg;
    ol_event_loop_run(g_cfg.loop);
    return NULL;
}

static int request_actor_beh(ol_actor_t *self, void *msg)
{
    (void)self;
    ol_ask_envelope_t *env = (ol_ask_envelope_t *)msg;
    if (!env || !env->reply) {
        if (msg) free(msg);
        return 0;
    }

    http_request_t req;
    const char    *raw     = (const char *)env->payload;
    size_t         raw_len = strlen(raw);

    if (http_parse_request(raw, raw_len, &req) != HTTP_PARSE_OK) {
        static const char *body = "400 Bad Request\n";
        char *resp = malloc(512);
        if (resp) {
            int n = http_build_response(400, "Bad Request", "text/plain",
                                        body, strlen(body), resp, 512);
            if (n > 0) {
                http_response_t *out = malloc(sizeof(*out));
                if (out) {
                    out->data = resp;
                    out->len  = (size_t)n;
                    ol_actor_reply_ok(env, out, http_response_destroy);
                    return 0;
                }
                free(resp);
            } else {
                free(resp);
            }
        }
        ol_actor_reply_error(env, -1);
        return 0;
    }

    router_ctx_t rctx = {
        .root        = g_cfg.root,
        .fpm_host    = g_cfg.fpm_host,
        .fpm_port    = g_cfg.fpm_port,
        .script_root = g_cfg.script_root,
    };

    size_t cap  = 512 * 1024;
    char  *resp = malloc(cap);
    if (!resp) {
        ol_actor_reply_error(env, -1);
        return 0;
    }

    long n = router_dispatch(&req, &rctx, resp, cap);
    if (n < 0) {
        free(resp);
        ol_actor_reply_error(env, -1);
        return 0;
    }

    http_response_t *out = malloc(sizeof(*out));
    if (!out) {
        free(resp);
        ol_actor_reply_error(env, -1);
        return 0;
    }
    out->data = resp;
    out->len  = (size_t)n;

    ol_actor_reply_ok(env, out, http_response_destroy);
    return 0;
}

static void handle_client(void *arg)
{
    ol_tcp_socket_t *cli = (ol_tcp_socket_t *)arg;
    if (!cli) return;

    int64_t deadline_ns = ol_deadline_from_ms(5000).when_ns;

    ol_future_t *rf = ol_tcp_socket_recv(cli, 8192, deadline_ns);
    if (!rf) { ol_tcp_socket_destroy(cli); return; }

    if (ol_future_await(rf, deadline_ns) != 1) {
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

    char *raw = malloc(nb->len + 1);
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

    ol_actor_t *actor = ol_actor_create(NULL, 16, free,
                                        request_actor_beh, NULL);
    if (!actor) {
        free(raw);
        ol_tcp_socket_destroy(cli);
        return;
    }
    ol_actor_start(actor);

    ol_future_t *af = ol_actor_ask(actor, raw);
    if (!af) {
        ol_actor_stop(actor);
        ol_actor_destroy(actor);
        ol_tcp_socket_destroy(cli);
        return;
    }

    if (ol_future_await(af, deadline_ns) == 1) {
        http_response_t *resp = (http_response_t *)ol_future_take_value(af);
        if (resp && resp->data && resp->len > 0) {
            ol_future_t *sf = ol_tcp_socket_send(cli, resp->data,
                                                 resp->len, deadline_ns);
            if (sf) {
                (void)ol_future_await(sf, deadline_ns);
                if (ol_future_state(sf) == OL_PROMISE_FULFILLED) {
                    free(ol_future_take_value(sf));
                }
                ol_future_destroy(sf);
            }
        }
        if (resp) http_response_destroy(resp);
    }
    ol_future_destroy(af);

    /* v1.3.2 fix: close the client socket BEFORE destroying the
     * actor.
     *
     * ol_actor_destroy() joins the actor's driver thread and frees
     * two arenas (the actor's private arena plus the process's
     * arena). On a warm machine that takes 15-20 ms. If the socket
     * stays open during that window, any client that waits for the
     * server's FIN before declaring the response complete includes
     * the cleanup time in its latency measurement. ab does exactly
     * that; curl does not.
     *
     * Closing the socket first sends FIN immediately. Actor cleanup
     * then runs on the worker thread without affecting what the
     * client measures. The order is safe: the response has already
     * been written and flushed by the send future's completion, and
     * the actor no longer needs the socket. */
    ol_tcp_socket_destroy(cli);

    ol_actor_stop(actor);
    ol_actor_destroy(actor);
}

static int env_int(const char *name, int fallback)
{
    const char *v = getenv(name);
    if (!v || !*v) return fallback;
    return atoi(v);
}

static const char *env_str(const char *name, const char *fallback)
{
    const char *v = getenv(name);
    return (v && *v) ? v : fallback;
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("gold_ws — OverLab HTTP/1.1 server\n");
    printf("=================================\n\n");

    g_cfg.port     = env_int("GOLD_WS_PORT",     DEFAULT_PORT);
    g_cfg.fpm_host = env_str("GOLD_WS_FPM_HOST", DEFAULT_FPM_HOST);
    g_cfg.fpm_port = env_int("GOLD_WS_FPM_PORT", DEFAULT_FPM_PORT);
    g_cfg.workers  = env_int("GOLD_WS_WORKERS",  DEFAULT_WORKERS);

    g_cfg.root = make_abs_path(env_str("GOLD_WS_ROOT", DEFAULT_ROOT),
                               g_root_buf, sizeof(g_root_buf));
    g_cfg.script_root = make_abs_path(
        env_str("GOLD_WS_SCRIPT_ROOT", DEFAULT_SCRIPT_ROOT),
        g_script_root_buf, sizeof(g_script_root_buf));

    printf("port         : %d\n",   g_cfg.port);
    printf("root         : %s\n",   g_cfg.root);
    printf("script root  : %s\n",   g_cfg.script_root);
    printf("php-fpm      : %s:%d\n", g_cfg.fpm_host, g_cfg.fpm_port);
    printf("workers      : %d\n\n", g_cfg.workers);

    if (access(g_cfg.script_root, F_OK) != 0) {
        fprintf(stderr,
                "warning: script root does not exist: %s\n"
                "         run gold_ws from demos/gold_ws/ or set "
                "GOLD_WS_SCRIPT_ROOT\n",
                g_cfg.script_root);
    }

    g_cfg.loop = ol_event_loop_create();
    if (!g_cfg.loop) {
        fprintf(stderr, "event loop create failed\n");
        return 1;
    }

    pthread_t loop_tid;
    if (pthread_create(&loop_tid, NULL, loop_thread, NULL) != 0) {
        fprintf(stderr, "event loop thread create failed\n");
        ol_event_loop_destroy(g_cfg.loop);
        return 1;
    }

    g_pool = ol_parallel_create((size_t)g_cfg.workers);
    if (!g_pool) {
        fprintf(stderr, "parallel pool create failed\n");
        ol_event_loop_stop(g_cfg.loop);
        pthread_join(loop_tid, NULL);
        ol_event_loop_destroy(g_cfg.loop);
        return 1;
    }

    ol_tcp_socket_t *srv = ol_tcp_socket_create(g_cfg.loop);
    if (!srv) {
        fprintf(stderr, "tcp socket create failed\n");
        goto cleanup;
    }

    if (ol_tcp_socket_open(srv, AF_INET) != 0) {
        fprintf(stderr, "tcp socket open failed\n");
        ol_tcp_socket_destroy(srv);
        goto cleanup;
    }

    ol_endpoint_t ep;
    memset(&ep, 0, sizeof(ep));
    strncpy(ep.host, "0.0.0.0", sizeof(ep.host) - 1);
    ep.port   = (uint16_t)g_cfg.port;
    ep.family = AF_INET;

    if (ol_tcp_socket_bind(srv, &ep) != 0) {
        fprintf(stderr, "bind failed on port %d\n", g_cfg.port);
        ol_tcp_socket_destroy(srv);
        goto cleanup;
    }
    if (ol_tcp_socket_listen(srv, 128) != 0) {
        fprintf(stderr, "listen failed\n");
        ol_tcp_socket_destroy(srv);
        goto cleanup;
    }

    printf("listening on 0.0.0.0:%d\n", g_cfg.port);
    printf("browse to http://localhost:%d/\n", g_cfg.port);
    printf("press Ctrl+C to stop.\n\n");

    for (;;) {
        int64_t      dl = ol_deadline_from_ms(60000).when_ns;
        ol_future_t *af = ol_tcp_socket_accept(srv, dl);
        if (!af) continue;

        if (ol_future_await(af, dl) != 1) {
            ol_future_destroy(af);
            continue;
        }

        ol_tcp_socket_t *cli = (ol_tcp_socket_t *)ol_future_take_value(af);
        ol_future_destroy(af);
        if (!cli) continue;

        if (ol_parallel_submit(g_pool, handle_client, cli) != 0) {
            ol_tcp_socket_destroy(cli);
        }
    }

cleanup:
    ol_event_loop_stop(g_cfg.loop);
    pthread_join(loop_tid, NULL);
    ol_parallel_destroy(g_pool);
    ol_event_loop_destroy(g_cfg.loop);
    return 0;
}
