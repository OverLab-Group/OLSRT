#include "network/ol_udp.h"

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
#include <arpa/inet.h>
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
    UDP_IDLE = 0,
    UDP_SENDING,
    UDP_RECEIVING
} udp_state_t;

typedef struct udp_pending {
    ol_promise_t *promise;
    size_t        want_len;
    void         *send_buf;
    struct sockaddr_storage to_ss;
    socklen_t     to_len;
    int64_t       deadline_ns;
} udp_pending_t;

struct ol_udp_socket {
    ol_event_loop_t *loop;
    ol_fd_t          fd;
    uint64_t         reg_id;
    udp_state_t      state;
    int              last_err;

    udp_pending_t    pend_send;
    udp_pending_t    pend_recv;

    ol_mutex_t       mu;
};

/* Utilities */
static void set_last_error(ol_udp_socket_t *s, int e) { s->last_err = e; }

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

/* IO callback */
static void udp_io_cb(ol_event_loop_t *loop, ol_ev_type_t type, int fd, void *ud) {
    (void)type; (void)fd;
    ol_udp_socket_t *s = (ol_udp_socket_t*)ud;
    if (!s) return;

    ol_mutex_lock(&s->mu);

    /* SENDING: writable => try to send */
    if (s->state == UDP_SENDING && s->pend_send.promise && s->pend_send.send_buf) {
        const uint8_t *ptr = (const uint8_t*)s->pend_send.send_buf;
        size_t len = s->pend_send.want_len;
        #if defined(_WIN32)
        int sent = sendto(s->fd, (const char*)ptr, (int)len, 0,
                          (struct sockaddr*)&s->pend_send.to_ss, s->pend_send.to_len);
        if (sent == SOCKET_ERROR) {
            int err = ol_last_error();
            if (err == WSAEWOULDBLOCK) { /* wait */ }
            else {
                set_last_error(s, err);
                s->state = UDP_IDLE;
                ol_promise_reject(s->pend_send.promise, -1);
                ol_promise_destroy(s->pend_send.promise);
                s->pend_send.promise = NULL;
                s->pend_send.send_buf = NULL;
                s->pend_send.want_len = 0;
            }
        } else {
            #else
            ssize_t sent = sendto(s->fd, ptr, len, 0,
                                  (struct sockaddr*)&s->pend_send.to_ss, s->pend_send.to_len);
            if (sent < 0) {
                int err = ol_last_error();
                if (err == EAGAIN || err == EWOULDBLOCK) { /* wait */ }
                else {
                    set_last_error(s, err);
                    s->state = UDP_IDLE;
                    ol_promise_reject(s->pend_send.promise, -1);
                    ol_promise_destroy(s->pend_send.promise);
                    s->pend_send.promise = NULL;
                    s->pend_send.send_buf = NULL;
                    s->pend_send.want_len = 0;
                }
            } else {
                #endif
                s->state = UDP_IDLE;
                /* fulfill with bytes_sent */
                size_t *sent_ptr = (size_t*)malloc(sizeof(size_t));
                *sent_ptr = (size_t)sent;
                (void)ol_promise_fulfill(s->pend_send.promise, sent_ptr, free);
                ol_promise_destroy(s->pend_send.promise);
                s->pend_send.promise = NULL;
                s->pend_send.send_buf = NULL;
                s->pend_send.want_len = 0;
            }
        }

        /* RECEIVING: readable => recvfrom */
        if (s->state == UDP_RECEIVING && s->pend_recv.promise && s->pend_recv.want_len > 0) {
            size_t want = s->pend_recv.want_len;
            uint8_t *buf = (uint8_t*)malloc(want);
            struct sockaddr_storage from; socklen_t fl = sizeof(from);
            #if defined(_WIN32)
            int got = recvfrom(s->fd, (char*)buf, (int)want, 0, (struct sockaddr*)&from, &fl);
            if (got == SOCKET_ERROR) {
                int err = ol_last_error();
                free(buf);
                if (err == WSAEWOULDBLOCK) { /* wait */ }
                else {
                    set_last_error(s, err);
                    s->state = UDP_IDLE;
                    ol_promise_reject(s->pend_recv.promise, -1);
                    ol_promise_destroy(s->pend_recv.promise);
                    s->pend_recv.promise = NULL;
                    s->pend_recv.want_len = 0;
                }
            } else {
                #else
                ssize_t got = recvfrom(s->fd, buf, want, 0, (struct sockaddr*)&from, &fl);
                if (got < 0) {
                    int err = ol_last_error();
                    free(buf);
                    if (err == EAGAIN || err == EWOULDBLOCK) { /* wait */ }
                    else {
                        set_last_error(s, err);
                        s->state = UDP_IDLE;
                        ol_promise_reject(s->pend_recv.promise, -1);
                        ol_promise_destroy(s->pend_recv.promise);
                        s->pend_recv.promise = NULL;
                        s->pend_recv.want_len = 0;
                    }
                } else {
                    #endif
                    /* Pack result: buf + from endpoint (heap blob) */
                    typedef struct {
                        ol_net_buf_t  *buf;
                        ol_endpoint_t  from;
                    } udp_result_t;
                    udp_result_t *res = (udp_result_t*)calloc(1, sizeof(udp_result_t));
                    ol_net_buf_t *nb = (ol_net_buf_t*)calloc(1, sizeof(ol_net_buf_t));
                    nb->data = buf;
                    nb->len  = (size_t)got;
                    nb->dtor = free;
                    res->buf = nb;

                    /* Translate from sockaddr to endpoint */
                    if (from.ss_family == AF_INET) {
                        struct sockaddr_in *sa = (struct sockaddr_in*)&from;
                        res->from.family = AF_INET;
                        res->from.port   = ntohs(sa->sin_port);
                        inet_ntop(AF_INET, &sa->sin_addr, res->from.host, sizeof(res->from.host));
                    } else if (from.ss_family == AF_INET6) {
                        struct sockaddr_in6 *sa6 = (struct sockaddr_in6*)&from;
                        res->from.family = AF_INET6;
                        res->from.port   = ntohs(sa6->sin6_port);
                        inet_ntop(AF_INET6, &sa6->sin6_addr, res->from.host, sizeof(res->from.host));
                    }

                    s->state = UDP_IDLE;
                    (void)ol_promise_fulfill(s->pend_recv.promise, res, (void(*)(void*))free);
                    ol_promise_destroy(s->pend_recv.promise);
                    s->pend_recv.promise = NULL;
                    s->pend_recv.want_len = 0;
                }
            }

            ol_mutex_unlock(&s->mu);
        }

        /* Public API */

        ol_udp_socket_t* ol_udp_socket_create(ol_event_loop_t *loop) {
            if (!loop) return NULL;
            ol_udp_socket_t *s = (ol_udp_socket_t*)calloc(1, sizeof(ol_udp_socket_t));
            if (!s) return NULL;
            s->loop = loop;
            s->fd = OL_INVALID_FD;
            s->reg_id = 0;
            s->state = UDP_IDLE;
            s->last_err = 0;
            ol_mutex_init(&s->mu);
            return s;
        }

        int ol_udp_socket_open(ol_udp_socket_t *s, int family) {
            if (!s) return -1;
            #if defined(_WIN32)
            ol_fd_t fd = WSASocketW((family == AF_INET6) ? AF_INET6 : AF_INET,
                                    SOCK_DGRAM, IPPROTO_UDP, NULL, 0, 0);
            if (fd == INVALID_SOCKET) { set_last_error(s, ol_last_error()); return -1; }
            #else
            ol_fd_t fd = socket((family == AF_INET6) ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
            if (fd < 0) { set_last_error(s, ol_last_error()); return -1; }
            #endif
            if (ol_set_nonblock(fd) != 0) { set_last_error(s, ol_last_error()); ol_close_fd(fd); return -1; }

            s->fd = fd;
            s->reg_id = ol_event_loop_register_io(s->loop, (int)fd, OL_POLL_IN | OL_POLL_OUT, udp_io_cb, s);
            return (s->reg_id != 0) ? 0 : -1;
        }

        int ol_udp_socket_bind(ol_udp_socket_t *s, const ol_endpoint_t *ep) {
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

        ol_future_t* ol_udp_socket_sendto(ol_udp_socket_t *s,
                                          const void *buf, size_t len,
                                          const ol_endpoint_t *to,
                                          int64_t deadline_ns)
        {
            if (!s || s->fd == OL_INVALID_FD || !buf || len == 0 || !to) return NULL;
            struct sockaddr_storage ss; socklen_t sl;
            if (ep_to_sockaddr(to, &ss, &sl) != 0) return NULL;

            ol_mutex_lock(&s->mu);
            if (s->state != UDP_IDLE || s->pend_send.promise) { ol_mutex_unlock(&s->mu); return NULL; }

            ol_promise_t *p = ol_promise_create(s->loop);
            if (!p) { ol_mutex_unlock(&s->mu); return NULL; }

            s->pend_send.promise  = p;
            s->pend_send.send_buf = (void*)buf;
            s->pend_send.want_len = len;
            s->pend_send.to_ss    = ss;
            s->pend_send.to_len   = sl;
            s->pend_send.deadline_ns = deadline_ns;
            s->state = UDP_SENDING;
            ol_mutex_unlock(&s->mu);

            (void)ol_event_loop_wake(s->loop);
            return ol_promise_get_future(p);
        }

        ol_future_t* ol_udp_socket_recvfrom(ol_udp_socket_t *s, size_t max_len, int64_t deadline_ns) {
            if (!s || s->fd == OL_INVALID_FD || max_len == 0) return NULL;

            ol_mutex_lock(&s->mu);
            if (s->state != UDP_IDLE || s->pend_recv.promise) { ol_mutex_unlock(&s->mu); return NULL; }

            ol_promise_t *p = ol_promise_create(s->loop);
            if (!p) { ol_mutex_unlock(&s->mu); return NULL; }

            s->pend_recv.promise  = p;
            s->pend_recv.want_len = max_len;
            s->pend_recv.deadline_ns = deadline_ns;
            s->state = UDP_RECEIVING;
            ol_mutex_unlock(&s->mu);

            (void)ol_event_loop_wake(s->loop);
            return ol_promise_get_future(p);
        }

        int ol_udp_socket_close(ol_udp_socket_t *s) {
            if (!s) return -1;
            ol_mutex_lock(&s->mu);
            if (s->reg_id) { (void)ol_event_loop_unregister(s->loop, s->reg_id); s->reg_id = 0; }
            if (s->fd != OL_INVALID_FD) {
                (void)ol_close_fd(s->fd);
                s->fd = OL_INVALID_FD;
            }
            if (s->pend_send.promise) { ol_promise_cancel(s->pend_send.promise); ol_promise_destroy(s->pend_send.promise); s->pend_send.promise = NULL; }
            if (s->pend_recv.promise) { ol_promise_cancel(s->pend_recv.promise); ol_promise_destroy(s->pend_recv.promise); s->pend_recv.promise = NULL; }
            s->state = UDP_IDLE;
            ol_mutex_unlock(&s->mu);
            return 0;
        }

        void ol_udp_socket_destroy(ol_udp_socket_t *s) {
            if (!s) return;
            (void)ol_udp_socket_close(s);
            ol_mutex_destroy(&s->mu);
            free(s);
        }

        bool ol_udp_socket_is_open(const ol_udp_socket_t *s) {
            return s && s->fd != OL_INVALID_FD;
        }

        int ol_udp_socket_fd(const ol_udp_socket_t *s) {
            return s ? (int)s->fd : -1;
        }

        int ol_udp_socket_last_error(const ol_udp_socket_t *s) {
            return s ? s->last_err : 0;
        }
