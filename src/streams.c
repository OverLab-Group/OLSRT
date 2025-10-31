/* OLSRT - Streams */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>
#include <string.h>

/* Forward declarations (poller API from section 5) */
int ol_poller_add(ol_poller_t *p, int fd, int events, ol_poller_cb cb, void *arg);
int ol_poller_mod(ol_poller_t *p, int fd, int events, ol_poller_cb cb, void *arg);
int ol_poller_del(ol_poller_t *p, int fd);

/* Internal representation */
typedef struct ol_stream_impl_s {
    ol_loop_t         *loop;
    ol_stream_kind_t   kind;
    ol_stream_opts_t   opts;
    ol_buf_t          *inbuf;
    ol_buf_t          *outbuf;
    int                fd;
    int                closed;

    /* Callbacks */
    ol_stream_data_cb  on_data;
    void              *on_data_arg;
    ol_stream_cb       on_writable;
    void              *on_writable_arg;
} ol_stream_impl_t;

/* Public wrapper maps 1:1 to impl for ABI stability */
struct ol_stream_s {
    ol_stream_impl_t impl;
};

/* ========================= Internal helpers ========================= */

/* Read from fd into a temporary buffer and fan out to user callback */
static inline void ol_stream_handle_read(ol_stream_impl_t *st) {
    uint8_t tmp[16384];
#ifdef _WIN32
    int rn = recv(st->fd, (char*)tmp, (int)sizeof(tmp), 0);
    ssize_t n = (rn >= 0) ? (ssize_t)rn : -1;
#else
    ssize_t n = read(st->fd, tmp, sizeof(tmp));
#endif
    if (n > 0 && st->on_data) {
        st->on_data((ol_stream_t*)st, tmp, (size_t)n, st->on_data_arg);
    }
}

/* Flush pending outbuf on write readiness, emit writable callback when drained */
static inline void ol_stream_handle_write(ol_stream_impl_t *st) {
    if (!st->outbuf || st->outbuf->len == 0) return;
#ifdef _WIN32
    int wn = send(st->fd, (const char*)st->outbuf->data, (int)st->outbuf->len, 0);
    ssize_t n = (wn >= 0) ? (ssize_t)wn : -1;
#else
    ssize_t n = write(st->fd, st->outbuf->data, st->outbuf->len);
#endif
    if (n > 0) {
        /* Shift remaining data */
        memmove(st->outbuf->data, st->outbuf->data + n, st->outbuf->len - (size_t)n);
        st->outbuf->len -= (size_t)n;

        /* Signal writable when buffer drains */
        if (st->outbuf->len == 0 && st->on_writable) {
            st->on_writable((ol_stream_t*)st, OL_OK, st->on_writable_arg);
            st->on_writable     = NULL;
            st->on_writable_arg = NULL;
        }
    }
}

/* Poller callback bridge */
static void ol_stream_evt_cb(int fd, int events, void *arg) {
    ol_stream_impl_t *st = (ol_stream_impl_t*)arg;
    if (!st || st->closed || st->fd != fd) return;

    if (events & OL_EVT_READ)  ol_stream_handle_read(st);
    if (events & OL_EVT_WRITE) ol_stream_handle_write(st);
}

/* Allocate and initialize a stream impl */
static ol_stream_impl_t* ol_stream_impl_alloc(ol_loop_t *loop, ol_stream_kind_t kind) {
    ol_stream_impl_t *st = (ol_stream_impl_t*)calloc(1, sizeof(*st));
    if (!st) return NULL;
    st->loop   = loop;
    st->kind   = kind;
    st->fd     = OL_INVALID_FD;
    st->inbuf  = ol_buf_alloc(8192);
    st->outbuf = ol_buf_alloc(8192);
    if (!st->inbuf || !st->outbuf) {
        if (st->inbuf)  ol_buf_free(st->inbuf);
        if (st->outbuf) ol_buf_free(st->outbuf);
        free(st);
        return NULL;
    }
    return st;
}

/* ============================== Lifecycle ============================== */

int ol_stream_close(ol_stream_t *st_) {
    if (!st_) return OL_ERR_STATE;
    ol_stream_impl_t *st = (ol_stream_impl_t*)st_;
    if (st->closed) return OL_ERR_STATE;
    st->closed = 1;

    if (st->fd != OL_INVALID_FD) {
        ol_poller_del(st->loop->poller, st->fd);
        close(st->fd);
        st->fd = OL_INVALID_FD;
    }
    return OL_OK;
}

/* ============================== Control ============================== */

int ol_stream_pause(ol_stream_t *st_) {
    if (!st_) return OL_ERR_STATE;
    ol_stream_impl_t *st = (ol_stream_impl_t*)st_;
    if (st->fd == OL_INVALID_FD) return OL_ERR_STATE;
    return ol_poller_mod(st->loop->poller, st->fd, OL_EVT_WRITE, ol_stream_evt_cb, st);
}

int ol_stream_resume(ol_stream_t *st_) {
    if (!st_) return OL_ERR_STATE;
    ol_stream_impl_t *st = (ol_stream_impl_t*)st_;
    if (st->fd == OL_INVALID_FD) return OL_ERR_STATE;
    return ol_poller_mod(st->loop->poller, st->fd, OL_EVT_READ | OL_EVT_WRITE, ol_stream_evt_cb, st);
}

size_t ol_stream_inbuf_len(ol_stream_t *st_) {
    ol_stream_impl_t *st = (ol_stream_impl_t*)st_;
    return (st && st->inbuf) ? st->inbuf->len : 0;
}

size_t ol_stream_outbuf_len(ol_stream_t *st_) {
    ol_stream_impl_t *st = (ol_stream_impl_t*)st_;
    return (st && st->outbuf) ? st->outbuf->len : 0;
}

/* ============================== I/O ============================== */

int ol_stream_read_start(ol_stream_t *st_, ol_stream_data_cb on_data, void *arg) {
    if (!st_ || !on_data) return OL_ERR_STATE;
    ol_stream_impl_t *st = (ol_stream_impl_t*)st_;
    st->on_data     = on_data;
    st->on_data_arg = arg;
    return ol_poller_mod(st->loop->poller, st->fd, OL_EVT_READ | OL_EVT_WRITE, ol_stream_evt_cb, st);
}

int ol_stream_read_stop(ol_stream_t *st_) {
    if (!st_) return OL_ERR_STATE;
    ol_stream_impl_t *st = (ol_stream_impl_t*)st_;
    st->on_data     = NULL;
    st->on_data_arg = NULL;
    return ol_poller_mod(st->loop->poller, st->fd, OL_EVT_WRITE, ol_stream_evt_cb, st);
}

int ol_stream_write(ol_stream_t *st_, const void *data, size_t n, ol_stream_cb cb, void *arg) {
    if (!st_ || (!data && n)) return OL_ERR_STATE;
    ol_stream_impl_t *st = (ol_stream_impl_t*)st_;
    if (n) {
        int rc = ol_buf_append(st->outbuf, data, n);
        if (rc != OL_OK) return rc;
    }
    st->on_writable     = cb;
    st->on_writable_arg = arg;
    return ol_poller_mod(st->loop->poller, st->fd, OL_EVT_READ | OL_EVT_WRITE, ol_stream_evt_cb, st);
}

int ol_stream_writev(ol_stream_t *st_, const ol_buf_t **iov, size_t iovcnt, ol_stream_cb cb, void *arg) {
    if (!st_ || (!iov && iovcnt)) return OL_ERR_STATE;
    ol_stream_impl_t *st = (ol_stream_impl_t*)st_;
    for (size_t i = 0; i < iovcnt; i++) {
        if (!iov[i]) continue;
        int rc = ol_buf_append(st->outbuf, iov[i]->data, iov[i]->len);
        if (rc != OL_OK) return rc;
    }
    st->on_writable     = cb;
    st->on_writable_arg = arg;
    return ol_poller_mod(st->loop->poller, st->fd, OL_EVT_READ | OL_EVT_WRITE, ol_stream_evt_cb, st);
}

/* ============================== Kind: TCP ============================== */

ol_stream_t* ol_stream_open_tcp(ol_loop_t *loop) {
    if (!loop) return NULL;
    ol_stream_impl_t *impl = ol_stream_impl_alloc(loop, OL_ST_TCP);
    if (!impl) return NULL;

    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { ol_buf_free(impl->inbuf); ol_buf_free(impl->outbuf); free(impl); return NULL; }

    impl->fd = fd;
    ol_set_nonblocking(impl->fd, 1);

    if (ol_poller_add(loop->poller, impl->fd, OL_EVT_READ | OL_EVT_WRITE, ol_stream_evt_cb, impl) != OL_OK) {
        close(fd); ol_buf_free(impl->inbuf); ol_buf_free(impl->outbuf); free(impl); return NULL;
    }

    return (ol_stream_t*)impl;
}

int ol_stream_connect(ol_stream_t *st_, const char *host, uint16_t port) {
    if (!st_ || !host) return OL_ERR_STATE;
    ol_stream_impl_t *st = (ol_stream_impl_t*)st_;
    if (st->fd == OL_INVALID_FD) return OL_ERR_STATE;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return OL_ERR_IO;

    int rc = connect(st->fd, res->ai_addr, (socklen_t)res->ai_addrlen);
    freeaddrinfo(res);
    (void)rc; /* readiness tracked via poller */
    return OL_OK;
}

int ol_stream_bind(ol_stream_t *st_, const char *host, uint16_t port) {
    if (!st_) return OL_ERR_STATE;
    ol_stream_impl_t *st = (ol_stream_impl_t*)st_;
    if (st->fd == OL_INVALID_FD) return OL_ERR_STATE;

    int yes = 1;
    (void)setsockopt(st->fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = host ? inet_addr(host) : INADDR_ANY;

    if (bind(st->fd, (struct sockaddr*)&addr, (socklen_t)sizeof(addr)) != 0) return OL_ERR_IO;
    return OL_OK;
}

int ol_stream_listen(ol_stream_t *st_, int backlog) {
    if (!st_) return OL_ERR_STATE;
    ol_stream_impl_t *st = (ol_stream_impl_t*)st_;
    return (listen(st->fd, backlog) == 0) ? OL_OK : OL_ERR_IO;
}

ol_stream_t* ol_stream_accept(ol_stream_t *listener_) {
    if (!listener_) return NULL;
    ol_stream_impl_t *lst = (ol_stream_impl_t*)listener_;
    int cfd = accept(lst->fd, NULL, NULL);
    if (cfd < 0) return NULL;

    ol_set_nonblocking(cfd, 1);

    ol_stream_impl_t *impl = ol_stream_impl_alloc(lst->loop, OL_ST_TCP);
    if (!impl) { close(cfd); return NULL; }

    impl->fd = cfd;
    if (ol_poller_add(lst->loop->poller, impl->fd, OL_EVT_READ | OL_EVT_WRITE, ol_stream_evt_cb, impl) != OL_OK) {
        close(cfd); ol_buf_free(impl->inbuf); ol_buf_free(impl->outbuf); free(impl); return NULL;
    }

    return (ol_stream_t*)impl;
}

/* ============================== Kind: UDP ============================== */

ol_stream_t* ol_stream_open_udp(ol_loop_t *loop) {
    if (!loop) return NULL;
    ol_stream_impl_t *impl = ol_stream_impl_alloc(loop, OL_ST_UDP);
    if (!impl) return NULL;

    int fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { ol_buf_free(impl->inbuf); ol_buf_free(impl->outbuf); free(impl); return NULL; }

    impl->fd = fd;
    ol_set_nonblocking(impl->fd, 1);

    if (ol_poller_add(loop->poller, impl->fd, OL_EVT_READ | OL_EVT_WRITE, ol_stream_evt_cb, impl) != OL_OK) {
        close(fd); ol_buf_free(impl->inbuf); ol_buf_free(impl->outbuf); free(impl); return NULL;
    }

    return (ol_stream_t*)impl;
}

/* ============================== Kind: File ============================== */

ol_stream_t* ol_stream_open_file(ol_loop_t *loop, const char *path, int flags) {
    if (!loop || !path) return NULL;
    ol_stream_impl_t *impl = ol_stream_impl_alloc(loop, OL_ST_FILE);
    if (!impl) return NULL;

    int fl = O_RDONLY;
    if (flags & 2) fl = O_WRONLY;
    if (flags & 3) fl = O_RDWR;

    int fd = open(path, fl);
    if (fd < 0) { ol_buf_free(impl->inbuf); ol_buf_free(impl->outbuf); free(impl); return NULL; }

    impl->fd = fd;
#ifndef _WIN32
    /* Non-blocking files via fcntl only on POSIX CRT */
    ol_set_nonblocking(impl->fd, 1);
#endif
    if (ol_poller_add(loop->poller, impl->fd, OL_EVT_READ | OL_EVT_WRITE, ol_stream_evt_cb, impl) != OL_OK) {
        close(fd); ol_buf_free(impl->inbuf); ol_buf_free(impl->outbuf); free(impl); return NULL;
    }

    return (ol_stream_t*)impl;
}

/* ============================== Kind: Pipe ============================== */

ol_stream_t* ol_stream_open_pipe(ol_loop_t *loop, int fd_read, int fd_write) {
    if (!loop) return NULL;
    ol_stream_impl_t *impl = ol_stream_impl_alloc(loop, OL_ST_PIPE);
    if (!impl) return NULL;

    impl->fd = (fd_read >= 0) ? fd_read : fd_write;
    if (impl->fd == OL_INVALID_FD) { ol_buf_free(impl->inbuf); ol_buf_free(impl->outbuf); free(impl); return NULL; }

#ifndef _WIN32
    ol_set_nonblocking(impl->fd, 1);
#endif
    if (ol_poller_add(loop->poller, impl->fd, OL_EVT_READ | OL_EVT_WRITE, ol_stream_evt_cb, impl) != OL_OK) {
        close(impl->fd); ol_buf_free(impl->inbuf); ol_buf_free(impl->outbuf); free(impl); return NULL;
    }

    return (ol_stream_t*)impl;
}

/* ======================== Pipe/transform facility ======================== */

typedef struct ol_pipe_ctx_s {
    ol_transform_fn fn;
    void           *arg;
    ol_stream_impl_t *dst;
} ol_pipe_ctx_t;

static void ol_pipe_on_data(ol_stream_t *src_st, const uint8_t *data, size_t n, void *arg) {
    (void)src_st;
    ol_pipe_ctx_t *pc = (ol_pipe_ctx_t*)arg;
    ol_buf_t *out = ol_buf_alloc(n ? (n * 2) : 16);
    if (!out) return;

    if (pc->fn) {
        if (pc->fn(data, n, out, pc->arg) != 0) {
            ol_buf_free(out);
            return;
        }
    } else {
        (void)ol_buf_append(out, data, n);
    }

    ol_stream_write((ol_stream_t*)pc->dst, out->data, out->len, NULL, NULL);
    ol_buf_free(out);
}

int ol_stream_pipe(ol_stream_t *src_, ol_stream_t *dst_, ol_transform_fn fn, void *arg) {
    if (!src_ || !dst_) return OL_ERR_STATE;
    ol_stream_impl_t *src = (ol_stream_impl_t*)src_;
    ol_stream_impl_t *dst = (ol_stream_impl_t*)dst_;

    ol_pipe_ctx_t *pc = (ol_pipe_ctx_t*)calloc(1, sizeof(*pc));
    if (!pc) return OL_ERR_ALLOC;
    pc->fn  = fn;
    pc->arg = arg;
    pc->dst = dst;

    return ol_stream_read_start(src_, ol_pipe_on_data, pc);
}
