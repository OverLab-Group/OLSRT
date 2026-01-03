#include "network/ol_tcp.h"

#include "ol_event_loop.h"
#include "ol_promise.h"
#include "ol_lock_mutex.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET ol_fd_t;
#define OL_INVALID_FD INVALID_SOCKET
static int ol_set_nonblock(ol_fd_t fd) {
    u_long nb = 1; return (ioctlsocket(fd, FIONBIO, &nb) == 0) ? 0 : -1;
}
static int ol_last_error(void) { return WSAGetLastError(); }
static int ol_close_fd(ol_fd_t fd) { return closesocket(fd) == 0 ? 0 : -1; }
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int ol_fd_t;
#define OL_INVALID_FD (-1)
static int ol_set_nonblock(ol_fd_t fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0) ? 0 : -1;
}
static int ol_last_error(void) { return errno; }
static int ol_close_fd(ol_fd_t fd) { return close(fd) == 0 ? 0 : -1; }
#endif

typedef enum {
    TCP_IDLE = 0,
    TCP_CONNECTING,
    TCP_ACCEPTING,
    TCP_SENDING,
    TCP_RECEIVING,
    TCP_LISTENING
} tcp_state_t;

typedef struct tcp_pending {
    /* One pending op at a time per direction for simplicity */
    ol_promise_t *promise;
    size_t want_len;      /* for send/recv */
    void  *send_buf;      /* for send */
    int64_t deadline_ns;
    /* For accept, result is a new socket pointer */
} tcp_pending_t;

struct ol_tcp_socket {
    ol_event_loop_t *loop;
    ol_fd_t          fd;
    uint64_t         reg_id;   /* loop registration id */
    tcp_state_t      state;
    int              last_err;

    /* Server: listening flag */
    bool             is_server;

    /* Pending ops */
    tcp_pending_t    pend_connect;
    tcp_pending_t    pend_accept;
    tcp_pending_t    pend_send;
    tcp_pending_t    pend_recv;

    /* Sync */
    ol_mutex_t       mu;
};

/* Utilities */
static void set_last_error(ol_tcp_socket_t *s, int e) { s->last_err = e; }
static void fulfill_and_reset(tcp_pending_t *p, int status_code, void *value, void (*dtor)(void*)) {
    if (!p->promise) return;
    if (status_code == 0) (void)ol_promise_fulfill(p->promise, value, dtor);
    else (void)ol_promise_reject(p->promise, status_code);
    ol_promise_destroy(p->promise);
    p->promise = NULL;
    p->send_buf = NULL;
    p->want_len = 0;
    p->deadline_ns = 0;
}

/* Map endpoint to sockaddr */
static int ep_to_sockaddr(const ol_endpoint_t *ep, struct sockaddr_storage *ss, socklen_t *ss_len) {
    if (!ep || !ss || !ss_len) return -1;
    memset(ss, 0, sizeof(*ss));
    if (ep->family == AF_INET) {
        struct sockaddr_in *sa = (struct sockaddr_in*)ss;
        sa->sin_family = AF_INET;
        sa->sin_port = htons(ep->port);
        #if defined(_WIN32)
        sa->sin_addr.s_addr = inet_addr(ep->host);
        if (sa->sin_addr.s_addr == INADDR_NONE) return -1;
        #else
        if (inet_pton(AF_INET, ep->host, &sa->sin_addr) != 1) return -1;
        #endif
        *ss_len = sizeof(struct sockaddr_in);
        return 0;
    } else if (ep->family == AF_INET6) {
        struct sockaddr_in6 *sa6 = (struct sockaddr_in6*)ss;
        sa6->sin6_family = AF_INET6;
        sa6->sin6_port = htons(ep->port);
        if (inet_pton(AF_INET6, ep->host, &sa6->sin6_addr) != 1) return -1;
        *ss_len = sizeof(struct sockaddr_in6);
        return 0;
    }
    return -1;
}

/* IO callback registered on event loop */
static void tcp_io_cb(ol_event_loop_t *loop, ol_ev_type_t type, int fd, void *ud) {
    (void)type; (void)fd;
    ol_tcp_socket_t *s = (ol_tcp_socket_t*)ud;
    if (!s) return;

    /* We handle readiness based on current state; protect with mutex */
    ol_mutex_lock(&s->mu);

    /* CONNECTING: check writability to complete connect */
    if (s->state == TCP_CONNECTING && s->pend_connect.promise) {
        /* On POSIX: writable + getsockopt(SO_ERROR) == 0 => connected */
        int err = 0; socklen_t elen = sizeof(err);
        if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, (void*)&err, &elen) == 0 && err == 0) {
            s->state = TCP_IDLE;
            fulfill_and_reset(&s->pend_connect, 0, NULL, NULL);
        } else {
            set_last_error(s, err ? err : ol_last_error());
            s->state = TCP_IDLE;
            fulfill_and_reset(&s->pend_connect, -1, NULL, NULL);
        }
    }

    /* ACCEPTING: readable on listener => accept one */
    if (s->state == TCP_ACCEPTING && s->pend_accept.promise && s->is_server) {
        struct sockaddr_storage ss; socklen_t sl = sizeof(ss);
        #if defined(_WIN32)
        ol_fd_t cfd = accept(s->fd, (struct sockaddr*)&ss, &sl);
        if (cfd == INVALID_SOCKET) {
            set_last_error(s, ol_last_error());
            fulfill_and_reset(&s->pend_accept, -1, NULL, NULL);
        } else {
            #else
            ol_fd_t cfd = accept(s->fd, (struct sockaddr*)&ss, &sl);
            if (cfd < 0) {
                set_last_error(s, ol_last_error());
                fulfill_and_reset(&s->pend_accept, -1, NULL, NULL);
            } else {
                #endif
                (void)ol_set_nonblock(cfd);
                /* Create child socket wrapper */
                ol_tcp_socket_t *child = (ol_tcp_socket_t*)calloc(1, sizeof(ol_tcp_socket_t));
                child->loop = s->loop;
                child->fd = cfd;
                child->reg_id = ol_event_loop_register_io(s->loop, (int)cfd, OL_POLL_IN | OL_POLL_OUT, tcp_io_cb, child);
                child->state = TCP_IDLE;
                child->last_err = 0;
                child->is_server = false;
                ol_mutex_init(&child->mu);
                /* Fulfill with new socket handle */
                s->state = TCP_IDLE;
                fulfill_and_reset(&s->pend_accept, 0, child, (void(*)(void*))free);
            }
        }

        /* SENDING: writable => try to write remaining */
        if (s->state == TCP_SENDING && s->pend_send.promise && s->pend_send.send_buf) {
            const uint8_t *ptr = (const uint8_t*)s->pend_send.send_buf;
            size_t left = s->pend_send.want_len;
            #if defined(_WIN32)
            int sent = send(s->fd, (const char*)ptr, (int)left, 0);
            if (sent == SOCKET_ERROR) {
                int err = ol_last_error();
                if (err == WSAEWOULDBLOCK) { /* wait next */ }
                else {
                    set_last_error(s, err);
                    s->state = TCP_IDLE;
                    fulfill_and_reset(&s->pend_send, -1, NULL, NULL);
                }
            } else {
                #else
                ssize_t sent = send(s->fd, ptr, left, 0);
                if (sent < 0) {
                    int err = ol_last_error();
                    if (err == EAGAIN || err == EWOULDBLOCK) { /* wait next */ }
                    else {
                        set_last_error(s, err);
                        s->state = TCP_IDLE;
                        fulfill_and_reset(&s->pend_send, -1, NULL, NULL);
                    }
                } else {
                    #endif
                    left -= (size_t)sent;
                    s->pend_send.want_len = left;
                    s->pend_send.send_buf = (void*)(ptr + sent);
                    if (left == 0) {
                        s->state = TCP_IDLE;
                        fulfill_and_reset(&s->pend_send, 0, NULL, NULL);
                    }
                }
            }

            /* RECEIVING: readable => read up to want_len */
            if (s->state == TCP_RECEIVING && s->pend_recv.promise && s->pend_recv.want_len > 0) {
                size_t want = s->pend_recv.want_len;
                uint8_t *buf = (uint8_t*)malloc(want);
                #if defined(_WIN32)
                int got = recv(s->fd, (char*)buf, (int)want, 0);
                if (got == SOCKET_ERROR) {
                    int err = ol_last_error();
                    free(buf);
                    if (err == WSAEWOULDBLOCK) { /* wait */ }
                    else {
                        set_last_error(s, err);
                        s->state = TCP_IDLE;
                        fulfill_and_reset(&s->pend_recv, -1, NULL, NULL);
                    }
                } else if (got == 0) {
                    /* closed by peer */
                    free(buf);
                    s->state = TCP_IDLE;
                    fulfill_and_reset(&s->pend_recv, -2, NULL, NULL);
                } else {
                    #else
                    ssize_t got = recv(s->fd, buf, want, 0);
                    if (got < 0) {
                        int err = ol_last_error();
                        free(buf);
                        if (err == EAGAIN || err == EWOULDBLOCK) { /* wait */ }
                        else {
                            set_last_error(s, err);
                            s->state = TCP_IDLE;
                            fulfill_and_reset(&s->pend_recv, -1, NULL, NULL);
                        }
                    } else if (got == 0) {
                        free(buf);
                        s->state = TCP_IDLE;
                        fulfill_and_reset(&s->pend_recv, -2, NULL, NULL);
                    } else {
                        #endif
                        ol_net_buf_t *out = (ol_net_buf_t*)calloc(1, sizeof(ol_net_buf_t));
                        out->data = buf;
                        out->len  = (size_t)got;
                        out->dtor = free;
                        s->state = TCP_IDLE;
                        fulfill_and_reset(&s->pend_recv, 0, out, (void(*)(void*))free);
                    }
                }

                ol_mutex_unlock(&s->mu);
            }

            /* Public API */

            ol_tcp_socket_t* ol_tcp_socket_create(ol_event_loop_t *loop) {
                if (!loop) return NULL;
                ol_tcp_socket_t *s = (ol_tcp_socket_t*)calloc(1, sizeof(ol_tcp_socket_t));
                if (!s) return NULL;
                s->loop = loop;
                s->fd = OL_INVALID_FD;
                s->reg_id = 0;
                s->state = TCP_IDLE;
                s->last_err = 0;
                s->is_server = false;
                ol_mutex_init(&s->mu);
                return s;
            }

            int ol_tcp_socket_open(ol_tcp_socket_t *s, int family) {
                if (!s) return -1;
                #if defined(_WIN32)
                ol_fd_t fd = WSASocketW((family == AF_INET6) ? AF_INET6 : AF_INET,
                                        SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
                if (fd == INVALID_SOCKET) { set_last_error(s, ol_last_error()); return -1; }
                #else
                ol_fd_t fd = socket((family == AF_INET6) ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
                if (fd < 0) { set_last_error(s, ol_last_error()); return -1; }
                #endif
                if (ol_set_nonblock(fd) != 0) { set_last_error(s, ol_last_error()); ol_close_fd(fd); return -1; }

                s->fd = fd;
                s->reg_id = ol_event_loop_register_io(s->loop, (int)fd, OL_POLL_IN | OL_POLL_OUT, tcp_io_cb, s);
                return (s->reg_id != 0) ? 0 : -1;
            }

            int ol_tcp_socket_bind(ol_tcp_socket_t *s, const ol_endpoint_t *ep) {
                if (!s || s->fd == OL_INVALID_FD || !ep) return -1;
                struct sockaddr_storage ss; socklen_t sl;
                if (ep_to_sockaddr(ep, &ss, &sl) != 0) return -1;

                #if defined(_WIN32)
                if (bind(s->fd, (struct sockaddr*)&ss, sl) == SOCKET_ERROR) {
                    set_last_error(s, ol_last_error()); return -1;
                }
                #else
                if (bind(s->fd, (struct sockaddr*)&ss, sl) != 0) {
                    set_last_error(s, ol_last_error()); return -1;
                }
                #endif
                return 0;
            }

            int ol_tcp_socket_listen(ol_tcp_socket_t *s, int backlog) {
                if (!s || s->fd == OL_INVALID_FD) return -1;
                #if defined(_WIN32)
                if (listen(s->fd, backlog) == SOCKET_ERROR) { set_last_error(s, ol_last_error()); return -1; }
                #else
                if (listen(s->fd, backlog) != 0) { set_last_error(s, ol_last_error()); return -1; }
                #endif
                s->is_server = true;
                return 0;
            }

            ol_future_t* ol_tcp_socket_accept(ol_tcp_socket_t *s, int64_t deadline_ns) {
                if (!s || !s->is_server) return NULL;
                ol_mutex_lock(&s->mu);
                if (s->state != TCP_IDLE || s->pend_accept.promise) { ol_mutex_unlock(&s->mu); return NULL; }

                ol_promise_t *p = ol_promise_create(s->loop);
                if (!p) { ol_mutex_unlock(&s->mu); return NULL; }
                s->pend_accept.promise = p;
                s->pend_accept.deadline_ns = deadline_ns;
                s->state = TCP_ACCEPTING;
                ol_mutex_unlock(&s->mu);

                ol_future_t *f = ol_promise_get_future(p);
                return f;
            }

            ol_future_t* ol_tcp_socket_connect(ol_tcp_socket_t *s, const ol_endpoint_t *ep, int64_t deadline_ns) {
                if (!s || s->fd == OL_INVALID_FD || !ep) return NULL;
                struct sockaddr_storage ss; socklen_t sl;
                if (ep_to_sockaddr(ep, &ss, &sl) != 0) return NULL;

                int r;
                #if defined(_WIN32)
                r = connect(s->fd, (struct sockaddr*)&ss, sl);
                if (r == SOCKET_ERROR) {
                    int err = ol_last_error();
                    if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) { set_last_error(s, err); return NULL; }
                }
                #else
                r = connect(s->fd, (struct sockaddr*)&ss, sl);
                if (r != 0) {
                    int err = ol_last_error();
                    if (err != EINPROGRESS) { set_last_error(s, err); return NULL; }
                }
                #endif

                ol_mutex_lock(&s->mu);
                if (s->state != TCP_IDLE || s->pend_connect.promise) { ol_mutex_unlock(&s->mu); return NULL; }
                ol_promise_t *p = ol_promise_create(s->loop);
                if (!p) { ol_mutex_unlock(&s->mu); return NULL; }
                s->pend_connect.promise = p;
                s->pend_connect.deadline_ns = deadline_ns;
                s->state = TCP_CONNECTING;
                ol_mutex_unlock(&s->mu);

                return ol_promise_get_future(p);
            }

            ol_future_t* ol_tcp_socket_send(ol_tcp_socket_t *s, const void *buf, size_t len, int64_t deadline_ns) {
                if (!s || s->fd == OL_INVALID_FD || !buf || len == 0) return NULL;

                ol_mutex_lock(&s->mu);
                if (s->state != TCP_IDLE || s->pend_send.promise) { ol_mutex_unlock(&s->mu); return NULL; }

                ol_promise_t *p = ol_promise_create(s->loop);
                if (!p) { ol_mutex_unlock(&s->mu); return NULL; }

                s->pend_send.promise  = p;
                s->pend_send.send_buf = (void*)buf;
                s->pend_send.want_len = len;
                s->pend_send.deadline_ns = deadline_ns;
                s->state = TCP_SENDING;
                ol_mutex_unlock(&s->mu);

                /* If immediately writable, callback will drain. Otherwise, poller wake ensures loop processes. */
                (void)ol_event_loop_wake(s->loop);

                return ol_promise_get_future(p);
            }

            ol_future_t* ol_tcp_socket_recv(ol_tcp_socket_t *s, size_t max_len, int64_t deadline_ns) {
                if (!s || s->fd == OL_INVALID_FD || max_len == 0) return NULL;

                ol_mutex_lock(&s->mu);
                if (s->state != TCP_IDLE || s->pend_recv.promise) { ol_mutex_unlock(&s->mu); return NULL; }

                ol_promise_t *p = ol_promise_create(s->loop);
                if (!p) { ol_mutex_unlock(&s->mu); return NULL; }
                s->pend_recv.promise = p;
                s->pend_recv.want_len = max_len;
                s->pend_recv.deadline_ns = deadline_ns;
                s->state = TCP_RECEIVING;
                ol_mutex_unlock(&s->mu);

                (void)ol_event_loop_wake(s->loop);
                return ol_promise_get_future(p);
            }

            int ol_tcp_socket_close(ol_tcp_socket_t *s) {
                if (!s) return -1;
                ol_mutex_lock(&s->mu);
                if (s->reg_id) { (void)ol_event_loop_unregister(s->loop, s->reg_id); s->reg_id = 0; }
                if (s->fd != OL_INVALID_FD) {
                    (void)ol_close_fd(s->fd);
                    s->fd = OL_INVALID_FD;
                }
                /* cancel pending */
                if (s->pend_connect.promise) { ol_promise_cancel(s->pend_connect.promise); ol_promise_destroy(s->pend_connect.promise); s->pend_connect.promise = NULL; }
                if (s->pend_accept.promise)  { ol_promise_cancel(s->pend_accept.promise);  ol_promise_destroy(s->pend_accept.promise);  s->pend_accept.promise = NULL; }
                if (s->pend_send.promise)    { ol_promise_cancel(s->pend_send.promise);    ol_promise_destroy(s->pend_send.promise);    s->pend_send.promise = NULL; }
                if (s->pend_recv.promise)    { ol_promise_cancel(s->pend_recv.promise);    ol_promise_destroy(s->pend_recv.promise);    s->pend_recv.promise = NULL; }
                s->state = TCP_IDLE;
                ol_mutex_unlock(&s->mu);
                return 0;
            }

            void ol_tcp_socket_destroy(ol_tcp_socket_t *s) {
                if (!s) return;
                (void)ol_tcp_socket_close(s);
                ol_mutex_destroy(&s->mu);
                free(s);
            }

            bool ol_tcp_socket_is_open(const ol_tcp_socket_t *s) {
                return s && s->fd != OL_INVALID_FD;
            }

            int ol_tcp_socket_fd(const ol_tcp_socket_t *s) {
                return s ? (int)s->fd : -1;
            }

            int ol_tcp_socket_last_error(const ol_tcp_socket_t *s) {
                return s ? s->last_err : 0;
            }
