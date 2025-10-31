/* OLSRT - HTTP helpers */

#include "compat.h"
#include "olsrt.h"

#include <string.h>
#include <stdlib.h>

/* Parse the request line: "<METHOD> <PATH> HTTP/1.1\r\n..."
 * Fills out->method and out->path to point to static storage for simplicity.
 * This parser is intentionally minimal and does not handle headers or query parsing.
 */
int ol_http_parse_request(ol_buf_t *in, ol_http_req_t *out) {
    if (!in || !out || !in->data || in->len == 0) return OL_ERR_STATE;

    const char *p = (const char*)in->data;
    const char *sp1 = strchr(p, ' ');
    if (!sp1) return OL_ERR_PROTO;

    const char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) return OL_ERR_PROTO;

    /* Method (up to 15 chars) and path (up to 511 chars) stored in static buffers */
    static char method[16];
    static char path[512];

    size_t mlen = (size_t)(sp1 - p);
    if (mlen == 0 || mlen >= sizeof(method)) return OL_ERR_PROTO;
    memcpy(method, p, mlen);
    method[mlen] = '\0';

    size_t plen = (size_t)(sp2 - (sp1 + 1));
    if (plen == 0 || plen >= sizeof(path)) return OL_ERR_PROTO;
    memcpy(path, sp1 + 1, plen);
    path[plen] = '\0';

    out->method = method;
    out->path   = path;
    return OL_OK;
}

/* Write a basic HTTP/1.1 response:
 * - Status line with provided code and reason
 * - Content-Length header, followed by CRLF CRLF
 * - Body bytes, if provided, then return OL_OK on success
 *
 * fd must be a writable descriptor (socket or file).
 */
int ol_http_write_response(int fd, const ol_http_res_t *res, const void *body, size_t n) {
    if (fd < 0 || !res) return OL_ERR_STATE;

    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        res->status,
                        res->reason ? res->reason : "OK",
                        n);

#ifdef _WIN32
    int hsent = send(fd, hdr, hlen, 0);
    if (hsent < 0) return OL_ERR_IO;
    if (n) {
        int bsent = send(fd, (const char*)body, (int)n, 0);
        if (bsent < 0) return OL_ERR_IO;
    }
#else
    if (write(fd, hdr, (size_t)hlen) < 0) return OL_ERR_IO;
    if (n && write(fd, body, n) < 0) return OL_ERR_IO;
#endif
    return OL_OK;
}
