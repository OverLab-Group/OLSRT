/**
 * @file http.c
 * @brief Minimal HTTP/1.1 parser and response builder implementation.
 */

#include "http.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static bool ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

/* Extract one CRLF-terminated line. Returns length without CRLF, or
 * -1 if no CRLF yet. */
static ssize_t next_line(const char *in, size_t in_len, size_t start,
                         size_t *line_end_out) {
    for (size_t i = start; i + 1 < in_len; ++i) {
        if (in[i] == '\r' && in[i + 1] == '\n') {
            *line_end_out = i;
            return (ssize_t)(i - start);
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Parser                                                             */
/* ------------------------------------------------------------------ */

http_parse_status_t http_parse_request(const char *in, size_t in_len,
                                       http_request_t *out) {
    memset(out, 0, sizeof(*out));

    size_t line_end = 0;
    ssize_t line_len = next_line(in, in_len, 0, &line_end);
    if (line_len < 0) return HTTP_PARSE_NEED_MORE;

    /* Parse request line: METHOD SP PATH SP VERSION */
    const char *p = in;
    const char *end = in + line_end;

    const char *m_start = p;
    while (p < end && *p != ' ') ++p;
    if (p == end) return HTTP_PARSE_BAD;
    size_t mlen = (size_t)(p - m_start);
    if (mlen == 0 || mlen >= HTTP_METHOD_MAX) return HTTP_PARSE_BAD;
    memcpy(out->method, m_start, mlen);
    out->method[mlen] = 0;
    ++p;  /* skip SP */

    const char *path_start = p;
    while (p < end && *p != ' ') ++p;
    if (p == end) return HTTP_PARSE_BAD;
    size_t plen = (size_t)(p - path_start);
    if (plen == 0 || plen >= HTTP_PATH_MAX) return HTTP_PARSE_BAD;
    memcpy(out->path, path_start, plen);
    out->path[plen] = 0;
    ++p;

    const char *v_start = p;
    while (p < end) ++p;
    size_t vlen = (size_t)(p - v_start);
    if (vlen == 0 || vlen >= sizeof(out->version)) return HTTP_PARSE_BAD;
    memcpy(out->version, v_start, vlen);
    out->version[vlen] = 0;

    /* Only HTTP/1.x is accepted. */
    if (strncmp(out->version, "HTTP/1.", 7) != 0) return HTTP_PARSE_BAD;

    size_t cursor = line_end + 2;

    /* Parse headers until blank line. */
    for (;;) {
        line_len = next_line(in, in_len, cursor, &line_end);
        if (line_len < 0) return HTTP_PARSE_NEED_MORE;

        if (line_len == 0) {
            out->parsed_len = line_end + 2;
            return HTTP_PARSE_OK;
        }

        if (out->header_count >= HTTP_HEADER_MAX) return HTTP_PARSE_BAD;

        /* Split on the first ':'. */
        const char *colon = memchr(in + cursor, ':', (size_t)line_len);
        if (!colon) return HTTP_PARSE_BAD;

        size_t name_len = (size_t)(colon - (in + cursor));
        if (name_len == 0 || name_len >= HTTP_NAME_MAX) return HTTP_PARSE_BAD;

        const char *vstart = colon + 1;
        while (vstart < in + line_end && *vstart == ' ') ++vstart;
        size_t value_len = (size_t)((in + line_end) - vstart);
        if (value_len >= HTTP_VALUE_MAX) return HTTP_PARSE_BAD;

        http_header_t *h = &out->headers[out->header_count++];
        memcpy(h->name, in + cursor, name_len);
        h->name[name_len] = 0;
        memcpy(h->value, vstart, value_len);
        h->value[value_len] = 0;

        cursor = line_end + 2;
    }
}

const char *http_request_header(const http_request_t *req,
                                const char *name) {
    for (size_t i = 0; i < req->header_count; ++i) {
        if (ieq(req->headers[i].name, name)) {
            return req->headers[i].value;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Response builder                                                   */
/* ------------------------------------------------------------------ */

int http_build_response(int status, const char *reason,
                        const char *content_type,
                        const void *body, size_t body_len,
                        char *out, size_t out_cap) {
    int n = snprintf(out, out_cap,
                     "HTTP/1.1 %d %s\r\n"
                     "Server: gold_ws/1.0 (OLSRT)\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     status, reason ? reason : "",
                     content_type ? content_type : "text/plain",
                     body_len);
    if (n < 0 || (size_t)n >= out_cap) return -1;
    if (body_len > 0) {
        if ((size_t)n + body_len > out_cap) return -1;
        memcpy(out + n, body, body_len);
        n += (int)body_len;
    }
    return n;
}
