/**
 * @file http.h
 * @brief Minimal HTTP/1.1 request parser and response builder.
 *
 * @details
 * Only the subset needed by the gold_ws server:
 *
 *   - request line  METHOD SP PATH SP VERSION CRLF
 *   - header lines  NAME ":" SP VALUE CRLF
 *   - headers are stored in a small fixed-size table (no dynamic
 *     allocation)
 *   - the body is not read (only GET is supported)
 *
 * The parser rejects anything that is not a well-formed HTTP/1.x
 * request. It does not implement chunked transfer encoding, upgrade,
 * or HTTP/2.
 */

#ifndef GOLD_WS_HTTP_H
#define GOLD_WS_HTTP_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define HTTP_METHOD_MAX 8
#define HTTP_PATH_MAX   512
#define HTTP_HEADER_MAX 32
#define HTTP_NAME_MAX   64
#define HTTP_VALUE_MAX  256

typedef struct {
    char name[HTTP_NAME_MAX];
    char value[HTTP_VALUE_MAX];
} http_header_t;

typedef struct {
    char method[HTTP_METHOD_MAX];
    char path[HTTP_PATH_MAX];
    char version[16];
    http_header_t headers[HTTP_HEADER_MAX];
    size_t header_count;
    size_t parsed_len;   /* bytes consumed from the input buffer */
} http_request_t;

typedef enum {
    HTTP_PARSE_OK = 0,
    HTTP_PARSE_NEED_MORE = 1,   /* partial request; caller may retry */
    HTTP_PARSE_BAD = -1
} http_parse_status_t;

/**
 * @brief Parse an HTTP request from a buffer.
 *
 * @param in      Input buffer.
 * @param in_len  Bytes available.
 * @param out     Parsed request; untouched on failure.
 * @return HTTP_PARSE_OK on success, HTTP_PARSE_NEED_MORE if the
 *         request is not yet complete, HTTP_PARSE_BAD on malformed
 *         input.
 */
http_parse_status_t http_parse_request(const char *in, size_t in_len,
                                       http_request_t *out);

/**
 * @brief Look up a header value (case-insensitive).
 *
 * @param req   Parsed request.
 * @param name  Header name.
 * @return Pointer into the request's header table, or NULL.
 */
const char *http_request_header(const http_request_t *req,
                                const char *name);

/**
 * @brief Build a minimal HTTP/1.1 response.
 *
 * @param status   Status code (200, 404, ...).
 * @param reason   Reason phrase ("OK", "Not Found", ...).
 * @param content_type Content-Type value.
 * @param body     Body bytes.
 * @param body_len Body length.
 * @param out      Output buffer.
 * @param out_cap  Output capacity.
 * @return Number of bytes written, or -1 if the buffer is too small.
 */
int http_build_response(int status, const char *reason,
                        const char *content_type,
                        const void *body, size_t body_len,
                        char *out, size_t out_cap);

#endif /* GOLD_WS_HTTP_H */
