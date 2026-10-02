/**
 * @file fpm.c
 * @brief Minimal FastCGI client for PHP-FPM.
 */

#include "fpm.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* FastCGI constants                                                  */
/* ------------------------------------------------------------------ */

#define FCGI_VERSION_1       1

#define FCGI_BEGIN_REQUEST   1
#define FCGI_ABORT_REQUEST   2
#define FCGI_END_REQUEST     3
#define FCGI_PARAMS          4
#define FCGI_STDIN           5
#define FCGI_STDOUT          6
#define FCGI_STDERR          7
#define FCGI_DATA            8
#define FCGI_GET_VALUES      9

#define FCGI_RESPONDER       1

#define FCGI_REQUEST_ID      1

typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint16_t request_id;
    uint16_t content_length;
    uint8_t  padding_length;
    uint8_t  reserved;
} fcgi_header_t;

typedef struct {
    uint16_t role;
    uint8_t  flags;
    uint8_t  reserved[5];
} fcgi_begin_request_t;

/* ------------------------------------------------------------------ */
/* Low-level I/O                                                      */
/* ------------------------------------------------------------------ */

static int read_full(int fd, void *buf, size_t n) {
    uint8_t *p = buf;
    while (n > 0) {
        ssize_t r = read(fd, p, n);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

static int write_full(int fd, const void *buf, size_t n) {
    const uint8_t *p = buf;
    while (n > 0) {
        ssize_t r = write(fd, p, n);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Record helpers                                                     */
/* ------------------------------------------------------------------ */

static int send_record(int fd, uint8_t type, const void *content,
                       uint16_t content_len, uint8_t padding_len) {
    fcgi_header_t h;
    memset(&h, 0, sizeof(h));
    h.version          = FCGI_VERSION_1;
    h.type             = type;
    h.request_id       = htons(FCGI_REQUEST_ID);
    h.content_length   = htons(content_len);
    h.padding_length   = padding_len;
    if (write_full(fd, &h, sizeof(h)) < 0) return -1;
    if (content_len > 0 && write_full(fd, content, content_len) < 0) {
        return -1;
    }
    if (padding_len > 0) {
        uint8_t pad[8] = {0};
        if (write_full(fd, pad, padding_len) < 0) return -1;
    }
    return 0;
}

/* Name-value pair encoding as per FastCGI spec. */
static size_t encode_len(uint8_t *buf, size_t n) {
    if (n < 128) {
        buf[0] = (uint8_t)n;
        return 1;
    }
    buf[0] = (uint8_t)((n >> 24) | 0x80);
    buf[1] = (uint8_t)((n >> 16) & 0xFF);
    buf[2] = (uint8_t)((n >> 8) & 0xFF);
    buf[3] = (uint8_t)(n & 0xFF);
    return 4;
}

static int send_param(int fd, const char *name, const char *value) {
    size_t nlen = strlen(name);
    size_t vlen = strlen(value);

    uint8_t buf[16];
    size_t n_enc = encode_len(buf, nlen);
    size_t v_enc = encode_len(buf + n_enc, vlen);
    size_t header_len = n_enc + v_enc;

    /* Build the full record in a temporary buffer. */
    size_t total = header_len + nlen + vlen;
    uint8_t *record = malloc(total);
    if (!record) return -1;
    memcpy(record, buf, header_len);
    memcpy(record + header_len, name, nlen);
    memcpy(record + header_len + nlen, value, vlen);

    int rc = send_record(fd, FCGI_PARAMS, record, (uint16_t)total, 0);
    free(record);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Connection                                                         */
/* ------------------------------------------------------------------ */

static int connect_to(const char *host, int port) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) return -1;

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                 */
/* ------------------------------------------------------------------ */

long fpm_execute(const char *host,
                 int port,
                 const char *script_file,
                 const char *request_uri,
                 const char *query_string,
                 const char *method,
                 const void *body,
                 size_t body_len,
                 char *out_buf,
                 size_t out_cap) {
    int fd = connect_to(host, port);
    if (fd < 0) {
        fprintf(stderr, "fpm: connect to %s:%d failed: %s\n",
                host, port, strerror(errno));
        return -1;
    }

    /* 1. BEGIN_REQUEST */
    fcgi_begin_request_t begin;
    memset(&begin, 0, sizeof(begin));
    begin.role  = htons(FCGI_RESPONDER);
    begin.flags = 0;   /* do not keep the connection open */
    if (send_record(fd, FCGI_BEGIN_REQUEST, &begin, sizeof(begin), 0) < 0) {
        close(fd);
        return -1;
    }

    /* 2. PARAMS */
    const char *content_type = "text/html";
    const char *content_length_str = NULL;
    char body_len_str[32];
    if (body_len > 0) {
        snprintf(body_len_str, sizeof(body_len_str), "%zu", body_len);
        content_length_str = body_len_str;
    }

    if (send_param(fd, "GATEWAY_INTERFACE", "CGI/1.1") < 0 ||
        send_param(fd, "SERVER_SOFTWARE",  "gold_ws/1.0 (OLSRT)") < 0 ||
        send_param(fd, "REQUEST_METHOD",   method ? method : "GET") < 0 ||
        send_param(fd, "REQUEST_URI",      request_uri ? request_uri : "/") < 0 ||
        send_param(fd, "SCRIPT_FILENAME",  script_file) < 0 ||
        send_param(fd, "SCRIPT_NAME",      request_uri ? request_uri : "/") < 0 ||
        send_param(fd, "QUERY_STRING",     query_string ? query_string : "") < 0 ||
        send_param(fd, "SERVER_PROTOCOL",  "HTTP/1.1") < 0 ||
        send_param(fd, "SERVER_NAME",      "localhost") < 0 ||
        send_param(fd, "SERVER_PORT",      "8080") < 0 ||
        send_param(fd, "REMOTE_ADDR",      "127.0.0.1") < 0 ||
        send_param(fd, "REMOTE_PORT",      "0") < 0 ||
        send_param(fd, "CONTENT_TYPE",     content_type) < 0 ||
        (content_length_str &&
         send_param(fd, "CONTENT_LENGTH", content_length_str) < 0)) {
        close(fd);
        return -1;
    }

    /* Terminate PARAMS with an empty record. */
    if (send_record(fd, FCGI_PARAMS, NULL, 0, 0) < 0) {
        close(fd);
        return -1;
    }

    /* 3. STDIN */
    if (body && body_len > 0) {
        if (send_record(fd, FCGI_STDIN, body, (uint16_t)body_len, 0) < 0) {
            close(fd);
            return -1;
        }
    }
    if (send_record(fd, FCGI_STDIN, NULL, 0, 0) < 0) {
        close(fd);
        return -1;
    }

    /* 4. Read the response. */
    long written = 0;
    for (;;) {
        fcgi_header_t h;
        if (read_full(fd, &h, sizeof(h)) < 0) break;

        uint16_t content_len = ntohs(h.content_length);
        uint8_t  pad_len     = h.padding_length;

        if (content_len > 0) {
            size_t to_read = content_len;
            while (to_read > 0) {
                size_t space = out_cap - (size_t)written;
                if (space == 0) {
                    /* Response overflow: stop reading, but drain
                     * to keep the socket consistent. */
                    char sink[512];
                    size_t chunk = to_read > sizeof(sink)
                                   ? sizeof(sink) : to_read;
                    if (read_full(fd, sink, chunk) < 0) goto done;
                    to_read -= chunk;
                    continue;
                }
                size_t chunk = to_read > space ? space : to_read;
                if (read_full(fd, out_buf + written, chunk) < 0) goto done;
                written += (long)chunk;
                to_read -= chunk;
            }
        }
        if (pad_len > 0) {
            char sink[8];
            if (read_full(fd, sink, pad_len) < 0) break;
        }
        if (h.type == FCGI_END_REQUEST) break;
    }

done:
    close(fd);
    return written;
}
