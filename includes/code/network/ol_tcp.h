#ifndef OL_TCP_H
#define OL_TCP_H

#ifdef __cplusplus
extern "C" {
    #endif

    #include <stddef.h>
    #include <stdint.h>
    #include <stdbool.h>

    /* Forward decls for OLSRT core types */
    typedef struct ol_event_loop ol_event_loop_t;
    typedef struct ol_future      ol_future_t;

    /* Poll masks (must match ol_poller.h) */
    #ifndef OL_POLL_IN
    #define OL_POLL_IN  0x01u
    #endif
    #ifndef OL_POLL_OUT
    #define OL_POLL_OUT 0x02u
    #endif
    #ifndef OL_POLL_ERR
    #define OL_POLL_ERR 0x04u
    #endif

    /* Endpoint abstraction */
    typedef struct ol_endpoint {
        char     host[256];  /* "127.0.0.1", "::1", or hostname (resolved externally) */
        uint16_t port;       /* host-order port */
        int      family;     /* AF_INET or AF_INET6 */
    } ol_endpoint_t;

    /* Buffer returned by recv operations */
    typedef struct ol_net_buf {
        void   *data;  /* owned by runtime unless take api introduced */
        size_t  len;
        void  (*dtor)(void*); /* optional; if non-NULL, user should call to free data */
    } ol_net_buf_t;

    /* TCP socket handle */
    typedef struct ol_tcp_socket ol_tcp_socket_t;

    /* Lifecycle */
    ol_tcp_socket_t* ol_tcp_socket_create(ol_event_loop_t *loop);
    int              ol_tcp_socket_open(ol_tcp_socket_t *s, int family);
    int              ol_tcp_socket_close(ol_tcp_socket_t *s);
    void             ol_tcp_socket_destroy(ol_tcp_socket_t *s);

    /* Server side */
    int         ol_tcp_socket_bind(ol_tcp_socket_t *s, const ol_endpoint_t *ep);
    int         ol_tcp_socket_listen(ol_tcp_socket_t *s, int backlog);
    /* Accept: returns future that fulfills with (ol_tcp_socket_t*) of the new connection. */
    ol_future_t* ol_tcp_socket_accept(ol_tcp_socket_t *s, int64_t deadline_ns);

    /* Client side */
    /* Connect: future fulfills with 0 on success; rejects on error code or -3 on timeout. */
    ol_future_t* ol_tcp_socket_connect(ol_tcp_socket_t *s, const ol_endpoint_t *ep, int64_t deadline_ns);

    /* Send/Recv */
    /* Send: future fulfills with bytes_sent (size_t) or rejects with error code. */
    ol_future_t* ol_tcp_socket_send(ol_tcp_socket_t *s, const void *buf, size_t len, int64_t deadline_ns);
    /* Recv: future fulfills with ol_net_buf_t* (caller should free via dtor) or rejects on error/timeout. */
    ol_future_t* ol_tcp_socket_recv(ol_tcp_socket_t *s, size_t max_len, int64_t deadline_ns);

    /* Introspection */
    bool     ol_tcp_socket_is_open(const ol_tcp_socket_t *s);
    int      ol_tcp_socket_fd(const ol_tcp_socket_t *s);
    int      ol_tcp_socket_last_error(const ol_tcp_socket_t *s);

    #ifdef __cplusplus
}
#endif
#endif /* OL_TCP_H */
