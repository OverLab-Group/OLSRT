/* libuv side: uv_tcp server + internal client thread. Built (~80 LoC). */
#include "bench_util.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include <unistd.h>

#define PORT 8191
#define N 3

static uv_loop_t *g_loop;
static uv_tcp_t g_srv;
static int g_served = 0;

static void on_cl(uv_handle_t *h) { (void)h; }
static void alloc_cb(uv_handle_t *h, size_t sz, uv_buf_t *b) {
    (void)h; b->base = malloc(sz); b->len = sz;
}
static void on_write(uv_write_t *w, int status) {
    (void)status;
    uv_close((uv_handle_t *)w->handle, on_cl);
    free(w->data); free(w);
}
static void read_cb(uv_stream_t *cl, ssize_t n, const uv_buf_t *b) {
    if (n < 0) { free(b->base); uv_close((uv_handle_t *)cl, on_cl); return; }
    const char *body = "Hello from libuv demo 16!\n";
    char resp[256];
    int rlen = snprintf(resp, sizeof(resp),
        "HTTP/1.1 200 OK\r\nServer: libuv-bench\r\n"
        "Content-Type: text/plain\r\nContent-Length: %zu\r\n"
        "Connection: close\r\n\r\n%s", strlen(body), body);
    uv_write_t *w = malloc(sizeof(*w));
    char *cp = malloc((size_t)rlen);
    memcpy(cp, resp, (size_t)rlen);
    uv_buf_t out = uv_buf_init(cp, (unsigned)rlen);
    w->data = cp;
    uv_write(w, cl, &out, 1, on_write);
    free(b->base);
    g_served++;
    if (g_served >= N) uv_close((uv_handle_t *)&g_srv, on_cl);
}
static void on_conn(uv_stream_t *s, int status) {
    if (status < 0) return;
    uv_tcp_t *cl = malloc(sizeof(*cl));
    uv_tcp_init(g_loop, cl);
    if (uv_accept(s, (uv_stream_t *)cl) == 0)
        uv_read_start((uv_stream_t *)cl, alloc_cb, read_cb);
    else uv_close((uv_handle_t *)cl, on_cl);
}

static void *client(void *arg) {
    (void)arg;
    sleep(1);
    for (int i = 0; i < N; i++) {
        uv_tcp_t *c = malloc(sizeof(*c));
        uv_tcp_init(g_loop, c);
        struct sockaddr_in a;
        uv_ip4_addr("127.0.0.1", PORT, &a);
        uv_connect_t *conn = malloc(sizeof(*conn));
        conn->data = c;
        uv_tcp_connect(conn, c, (const struct sockaddr *)&a, NULL);
        sleep(1);
        uv_close((uv_handle_t *)c, on_cl);
        free(c);
        free(conn);
    }
    return NULL;
}

int main(void) {
    printf("libuv/16_full_stack: built (uv_tcp + internal client)\n");
    printf("%d internal requests\n\n", N);
    uint64_t t0 = bench_now_ns();
    g_loop = uv_default_loop();
    struct sockaddr_in a;
    uv_ip4_addr("127.0.0.1", PORT, &a);
    uv_tcp_init(g_loop, &g_srv);
    uv_tcp_bind(&g_srv, (const struct sockaddr *)&a, 0);
    uv_listen((uv_stream_t *)&g_srv, 16, on_conn);
    pthread_t ct;
    pthread_create(&ct, NULL, client, NULL);
    uv_run(g_loop, UV_RUN_DEFAULT);
    pthread_join(ct, NULL);
    printf("requests_served:    %d\n", g_served);
    uv_loop_close(g_loop);
    bench_footer(t0, g_served, "requests");
    return 0;
}
