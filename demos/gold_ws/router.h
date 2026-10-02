/**
 * @file router.h
 * @brief Request routing and handler dispatch.
 */

#ifndef GOLD_WS_ROUTER_H
#define GOLD_WS_ROUTER_H

#include "http.h"

#include <stddef.h>

typedef struct {
    const char *root;        /* static file root */
    const char *fpm_host;    /* PHP-FPM host */
    int         fpm_port;    /* PHP-FPM port */
    const char *script_root; /* PHP script root (absolute path) */
} router_ctx_t;

/**
 * @brief Dispatch one HTTP request and produce a response.
 *
 * @param req  Parsed request.
 * @param ctx  Router context.
 * @param out  Response buffer.
 * @param cap  Response capacity.
 * @return Bytes written, or -1 on error.
 */
long router_dispatch(const http_request_t *req,
                     const router_ctx_t *ctx,
                     char *out, size_t cap);

#endif /* GOLD_WS_ROUTER_H */
