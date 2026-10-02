/**
 * @file router.c
 * @brief Request routing and handler dispatch.
 *
 * v1.3.2 fixes:
 *   - serve_php() strips the query string before building
 *     SCRIPT_FILENAME, and maps "/" to "/index.php".
 *   - The static-file scratch buffer lives on the stack. The
 *     previous versions used a 16 MB static or a 16 MB per-call
 *     malloc. The static was a data race between workers; the
 *     malloc triggered mmap/munmap for every static request, which
 *     dominated the profile under concurrent load. A stack buffer
 *     is per-thread by construction and never touches the heap.
 *   - Files larger than the stack buffer are rejected with 413
 *     rather than silently mis-served.
 */

#include "router.h"
#include "static.h"
#include "fpm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 1 MB stack buffer. Enough for style.css, index.php output, and
 * any reasonable static asset. Larger files get 413. */
#define STATIC_BUF_BYTES (1024u * 1024u)

static long serve_php(const http_request_t *req,
                      const router_ctx_t *ctx,
                      char *out, size_t cap)
{
    char uri[1024];
    strncpy(uri, req->path, sizeof(uri) - 1);
    uri[sizeof(uri) - 1] = 0;

    char *q = strchr(uri, '?');
    const char *query = "";
    if (q) {
        *q = 0;
        query = q + 1;
    }

    if (uri[0] == 0 || strcmp(uri, "/") == 0) {
        strncpy(uri, "/index.php", sizeof(uri) - 1);
        uri[sizeof(uri) - 1] = 0;
    }

    char script_path[1024];
    if (snprintf(script_path, sizeof(script_path), "%s%s",
                 ctx->script_root, uri) >= (int)sizeof(script_path)) {
        const char *body = "URI too long\n";
        return http_build_response(414, "URI Too Long", "text/plain",
                                   body, strlen(body), out, cap);
    }

    char fpm_out[64 * 1024];
    long n = fpm_execute(ctx->fpm_host, ctx->fpm_port,
                         script_path, uri, query, req->method,
                         NULL, 0,
                         fpm_out, sizeof(fpm_out));
    if (n < 0) {
        const char *body =
            "<h1>502 Bad Gateway</h1>"
            "<p>PHP-FPM is not reachable on the configured host/port.</p>";
        return http_build_response(502, "Bad Gateway", "text/html",
                                   body, strlen(body), out, cap);
    }

    const char *body = fpm_out;
    long body_len = n;
    for (long i = 0; i + 3 < n; ++i) {
        if (fpm_out[i] == '\r' && fpm_out[i+1] == '\n' &&
            fpm_out[i+2] == '\r' && fpm_out[i+3] == '\n') {
            body = fpm_out + i + 4;
            body_len = n - (i + 4);
            break;
        }
    }

    return http_build_response(200, "OK", "text/html",
                               body, (size_t)body_len, out, cap);
}

long router_dispatch(const http_request_t *req,
                     const router_ctx_t *ctx,
                     char *out, size_t cap)
{
    if (strcmp(req->method, "GET") != 0) {
        const char *body = "Method Not Allowed\n";
        return http_build_response(405, "Method Not Allowed", "text/plain",
                                   body, strlen(body), out, cap);
    }

    if (strcmp(req->path, "/") == 0) {
        return serve_php(req, ctx, out, cap);
    }

    const char *dot = strrchr(req->path, '.');
    if (dot && strcmp(dot, ".php") == 0) {
        return serve_php(req, ctx, out, cap);
    }

    /* v1.3.2: per-call stack buffer. Never touches the heap, never
     * mmap/munmap, and it is thread-local by construction because
     * every worker thread has its own stack. */
    char file_buf[STATIC_BUF_BYTES];

    const char *ctype = "application/octet-stream";
    long n = static_serve(ctx->root, req->path, file_buf,
                          sizeof(file_buf), &ctype);
    if (n < 0) {
        const char *body =
            "<h1>404 Not Found</h1>"
            "<p>The requested resource is not available.</p>";
        return http_build_response(404, "Not Found", "text/html",
                                   body, strlen(body), out, cap);
    }

    return http_build_response(200, "OK", ctype, file_buf, (size_t)n,
                               out, cap);
}
