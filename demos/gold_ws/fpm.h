/**
 * @file fpm.h
 * @brief Minimal FastCGI client for PHP-FPM.
 *
 * @details
 * Opens a TCP connection to a PHP-FPM pool (default 127.0.0.1:9000),
 * sends one request in the FastCGI record format, and reads back the
 * response bytes. Only the record types and fields required by
 * PHP-FPM are implemented.
 *
 * The client is intentionally synchronous and per-request; a
 * connection pool is on the v1.3.4 roadmap with ORoutines.
 */

#ifndef GOLD_WS_FPM_H
#define GOLD_WS_FPM_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Execute a PHP script through PHP-FPM.
 *
 * @param host        PHP-FPM host (e.g. "127.0.0.1").
 * @param port        PHP-FPM port (e.g. 9000).
 * @param script_file Absolute path to the .php file on disk, as
 *                    PHP-FPM sees it (SCRIPT_FILENAME).
 * @param request_uri The URI that triggered the request.
 * @param query_string The query string (may be empty).
 * @param method      "GET", "POST", ...
 * @param body        Request body (may be NULL for GET).
 * @param body_len    Body length.
 * @param out_buf     Output buffer for the raw FastCGI stdout stream.
 * @param out_cap     Output capacity.
 * @return Number of response bytes on success, -1 on error.
 */
long fpm_execute(const char *host,
                 int port,
                 const char *script_file,
                 const char *request_uri,
                 const char *query_string,
                 const char *method,
                 const void *body,
                 size_t body_len,
                 char *out_buf,
                 size_t out_cap);

#endif /* GOLD_WS_FPM_H */
