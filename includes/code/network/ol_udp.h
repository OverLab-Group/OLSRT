#ifndef OL_UDP_H
#define OL_UDP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct ol_event_loop ol_event_loop_t;
typedef struct ol_future ol_future_t;

typedef struct ol_endpoint {
    char host[256];
    uint16_t port;
    int family;
} ol_endpoint_t;

typedef struct ol_net_buf {
    void* data;
    size_t len;
    void (*dtor)(void*);
} ol_net_buf_t;

typedef struct ol_udp_socket ol_udp_socket_t;

/* Lifecycle */
ol_udp_socket_t* ol_udp_socket_create(ol_event_loop_t* loop);
int ol_udp_socket_open(ol_udp_socket_t* s, int family);
int ol_udp_socket_bind(ol_udp_socket_t* s, const ol_endpoint_t* ep);
int ol_udp_socket_close(ol_udp_socket_t* s);
void ol_udp_socket_destroy(ol_udp_socket_t* s);

/* Sendto/Recvfrom */
/* Sendto: fulfills with bytes_sent (size_t) or rejects with error code. */
ol_future_t* ol_udp_socket_sendto(ol_udp_socket_t* s,
                                  const void* buf,
                                  size_t len,
                                  const ol_endpoint_t* to,
                                  int64_t deadline_ns);

/* Recvfrom: fulfills with a struct { ol_net_buf_t* buf; ol_endpoint_t from } marshalled as a heap blob. */
ol_future_t*
ol_udp_socket_recvfrom(ol_udp_socket_t* s, size_t max_len, int64_t deadline_ns);

/* Introspection */
bool ol_udp_socket_is_open(const ol_udp_socket_t* s);
int ol_udp_socket_fd(const ol_udp_socket_t* s);
int ol_udp_socket_last_error(const ol_udp_socket_t* s);

#ifdef __cplusplus
}
#endif
#endif /* OL_UDP_H */
