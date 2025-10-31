#ifndef OLSRT_H
#define OLSRT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ============================= Version & Errors ============================= */

#define OL_VERSION_MAJOR 4
#define OL_VERSION_MINOR 0
#define OL_VERSION_PATCH 0

typedef enum {
  OL_OK          = 0,
  OL_ERR_GENERIC = -1,
  OL_ERR_ALLOC   = -2,
  OL_ERR_STATE   = -3,
  OL_ERR_IO      = -4,
  OL_ERR_TIMEOUT = -5,
  OL_ERR_CANCELED= -6,
  OL_ERR_CLOSED  = -7,
  OL_ERR_AGAIN   = -8,
  OL_ERR_PROTO   = -9,
  OL_ERR_NOTSUP  = -10,
  OL_ERR_CONFIG  = -11,
  OL_ERR_ARG     = -12,
  OL_ERR_RANGE   = -13,
  OL_ERR_INTERNAL= -14
} ol_err_t;

/* =============================== Feature flags ============================== */

typedef struct ol_features_s {
  int fibers_available;
  int epoll_available;
  int kqueue_available;
  int iocp_available;
  int tls_available;
  int http_available;
  int ws_available;
} ol_features_t;

const ol_features_t* ol_features(void);

/* ================================= Config =================================== */

typedef struct ol_config_s {
  int  event_loop_enabled;
  int  debug;
  int  max_events;
  int  max_workers;
  int  allow_blocking;
  int  poller_hint;          /* 0 auto, 1 epoll, 2 kqueue, 3 iocp */
  int  fiber_await;
  int  default_timeout_ms;
} ol_config_t;

const ol_config_t* ol_config_get(void);
int  ol_config_set(const ol_config_t *cfg);

/* ================================= Logging ================================== */

typedef void (*ol_log_fn)(int level, const char *msg);
void ol_set_logger(ol_log_fn fn);
void ol_log(int level, const char *fmt, ...);

/* ================================== Time ==================================== */

uint64_t ol_now_ms(void);
uint64_t ol_monotonic_ms(void);

/* ================================ Buffers =================================== */

typedef struct ol_buf_s {
  uint8_t *data;
  size_t   len;
  size_t   cap;
} ol_buf_t;

ol_buf_t* ol_buf_alloc(size_t cap);
int       ol_buf_reserve(ol_buf_t *b, size_t cap);
int       ol_buf_append(ol_buf_t *b, const void *src, size_t n);
int       ol_buf_slice(ol_buf_t *b, size_t off, size_t n, ol_buf_t **out);
void      ol_buf_clear(ol_buf_t *b);
void      ol_buf_free(ol_buf_t *b);

/* ================================= Arena ==================================== */

typedef struct ol_arena_s ol_arena_t;

ol_arena_t* ol_arena_create(size_t block_size);
void*       ol_arena_alloc(ol_arena_t *a, size_t n);
void        ol_arena_reset(ol_arena_t *a);
void        ol_arena_destroy(ol_arena_t *a);

/* ============================== Cancellation ================================ */

typedef void (*ol_cleanup_fn)(void *arg);
typedef struct ol_cancel_s ol_cancel_t;

ol_cancel_t* ol_cancel_create(void);
int          ol_cancel_register(ol_cancel_t *c, ol_cleanup_fn fn, void *arg);
int          ol_cancel_trigger(ol_cancel_t *c, ol_err_t reason);
int          ol_cancel_is_triggered(ol_cancel_t *c);
ol_err_t     ol_cancel_reason(ol_cancel_t *c);

/* ================================= Deadlines ================================= */

typedef struct ol_deadline_s {
  uint64_t at_ms;   /* absolute monotonic deadline */
} ol_deadline_t;

static inline ol_deadline_t ol_deadline_from_now(uint64_t delta_ms) {
  ol_deadline_t d; d.at_ms = ol_monotonic_ms() + delta_ms; return d;
}

/* ================================= Poller =================================== */

typedef struct ol_poller_s ol_poller_t;
typedef void (*ol_poller_cb)(int fd, int events, void *arg);

#define OL_EVT_READ   0x01
#define OL_EVT_WRITE  0x02
#define OL_EVT_ERROR  0x04

ol_poller_t* ol_poller_create(int hint, int max_events);
void         ol_poller_destroy(ol_poller_t *p);
int          ol_poller_add(ol_poller_t *p, int fd, int events, ol_poller_cb cb, void *arg);
int          ol_poller_mod(ol_poller_t *p, int fd, int events, ol_poller_cb cb, void *arg);
int          ol_poller_del(ol_poller_t *p, int fd);
int          ol_poller_wait(ol_poller_t *p, int timeout_ms);

/* ================================== Loop ==================================== */

typedef struct ol_loop_s ol_loop_t;
typedef void (*ol_task_fn)(void *arg);

typedef struct ol_loop_opts_s {
  int enable_debug;
  int allow_blocking;
  int max_events;
  int poller_hint;
} ol_loop_opts_t;

ol_loop_t* ol_loop_create(const ol_loop_opts_t *opts);
void       ol_loop_destroy(ol_loop_t *loop);
int        ol_loop_run(ol_loop_t *loop);
int        ol_loop_stop(ol_loop_t *loop);
int        ol_loop_tick(ol_loop_t *loop);
int        ol_loop_post(ol_loop_t *loop, ol_task_fn fn, void *arg);

/* ================================= Futures ================================== */

typedef enum {
  OL_FUT_PENDING = 0,
  OL_FUT_RESOLVED,
  OL_FUT_REJECTED,
  OL_FUT_CANCELED
} ol_future_state_t;

typedef struct ol_future_s ol_future_t;
typedef void (*ol_future_cb)(ol_future_t *f, void *arg);

ol_future_t*      ol_future_create(ol_loop_t *loop);
int               ol_future_then(ol_future_t *f, ol_future_cb cb, void *arg);
int               ol_future_resolve(ol_future_t *f, void *value);
int               ol_future_reject(ol_future_t *f, ol_err_t err);
int               ol_future_cancel(ol_future_t *f, ol_err_t reason);
ol_future_state_t ol_future_state(ol_future_t *f);
void*             ol_future_value(ol_future_t *f);
ol_err_t          ol_future_error(ol_future_t *f);

/* ================================= Timers =================================== */

typedef struct ol_timer_s ol_timer_t;
typedef void (*ol_timer_cb)(ol_timer_t *t, void *arg);

ol_timer_t* ol_timer_start(ol_loop_t *loop, uint64_t delay_ms, uint64_t period_ms,
                           ol_timer_cb cb, void *arg);
int         ol_timer_stop(ol_timer_t *t);
int         ol_timer_is_active(ol_timer_t *t);

/* ================================= Streams ================================== */

typedef enum {
  OL_ST_TCP,
  OL_ST_UDP,
  OL_ST_FILE,
  OL_ST_PIPE
} ol_stream_kind_t;

typedef struct ol_stream_opts_s {
  size_t read_high_watermark;
  size_t write_high_watermark;
  int    nonblocking;
} ol_stream_opts_t;

typedef struct ol_stream_s ol_stream_t;

typedef void (*ol_stream_cb)(ol_stream_t *st, ol_err_t status, void *arg);
typedef void (*ol_stream_data_cb)(ol_stream_t *st, const uint8_t *data, size_t n, void *arg);

ol_stream_t* ol_stream_open_tcp(ol_loop_t *loop);
ol_stream_t* ol_stream_open_udp(ol_loop_t *loop);
ol_stream_t* ol_stream_open_file(ol_loop_t *loop, const char *path, int flags);
ol_stream_t* ol_stream_open_pipe(ol_loop_t *loop, int fd_read, int fd_write);

int          ol_stream_close(ol_stream_t *st);

int          ol_stream_connect(ol_stream_t *st, const char *host, uint16_t port);
int          ol_stream_bind(ol_stream_t *st, const char *host, uint16_t port);
int          ol_stream_listen(ol_stream_t *st, int backlog);
ol_stream_t* ol_stream_accept(ol_stream_t *listener);

int          ol_stream_read_start(ol_stream_t *st, ol_stream_data_cb on_data, void *arg);
int          ol_stream_read_stop(ol_stream_t *st);

int          ol_stream_write(ol_stream_t *st, const void *data, size_t n, ol_stream_cb cb, void *arg);
int          ol_stream_writev(ol_stream_t *st, const ol_buf_t **iov, size_t iovcnt, ol_stream_cb cb, void *arg);

int          ol_stream_pause(ol_stream_t *st);
int          ol_stream_resume(ol_stream_t *st);

size_t       ol_stream_inbuf_len(ol_stream_t *st);
size_t       ol_stream_outbuf_len(ol_stream_t *st);

/* Pipe/transform (in-stream processing) */
typedef int (*ol_transform_fn)(const uint8_t *in, size_t inlen, ol_buf_t *out, void *arg);
int          ol_stream_pipe(ol_stream_t *src, ol_stream_t *dst, ol_transform_fn fn, void *arg);

/* ============================== TLS & Net utils ============================= */

int ol_stream_enable_tls(ol_stream_t *st, const char *cert_file, const char *key_file);
int ol_stream_disable_tls(ol_stream_t *st);

int ol_set_nonblocking(int fd, int nb);
int ol_tcp_socket(void);
int ol_udp_socket(void);

/* =========================== WebSocket framing (RFC6455) ===================== */

typedef void (*ol_ws_msg_cb)(ol_stream_t *st, const uint8_t *data, size_t len, int is_text, void *arg);

int ol_ws_handshake_server(ol_stream_t *st, const char *req_headers);
int ol_ws_handshake_client(ol_stream_t *st, const char *host, const char *path);

int ol_ws_send_text(ol_stream_t *st, const char *text, size_t len);
int ol_ws_send_binary(ol_stream_t *st, const uint8_t *data, size_t len);

int ol_ws_on_message(ol_stream_t *st, ol_ws_msg_cb cb, void *arg);

int ol_ws_ping(ol_stream_t *st, const void *data, size_t len);
int ol_ws_close(ol_stream_t *st, uint16_t code, const char *reason);

int  ol_ws_is_open(ol_stream_t *st);
int  ol_ws_is_client(ol_stream_t *st);
int  ol_ws_is_server(ol_stream_t *st);

/* =================================== HTTP =================================== */

typedef struct ol_http_req_s {
  const char *method;
  const char *path;
} ol_http_req_t;

typedef struct ol_http_res_s {
  int status;
  const char *reason;
} ol_http_res_t;

int ol_http_parse_request(ol_buf_t *in, ol_http_req_t *out);
int ol_http_write_response(int fd, const ol_http_res_t *res, const void *body, size_t n);

/* ================================= Channels ================================= */

typedef struct ol_channel_s ol_channel_t;

typedef struct ol_channel_opts_s {
  size_t capacity;
} ol_channel_opts_t;

ol_channel_t* ol_channel_create(const ol_channel_opts_t *opts);
int           ol_channel_send(ol_channel_t *ch, void *msg, uint64_t timeout_ms);
void*         ol_channel_recv(ol_channel_t *ch, uint64_t timeout_ms);
int           ol_channel_close(ol_channel_t *ch);

/* ================================== Actors ================================== */

typedef struct ol_actor_s ol_actor_t;
typedef void (*ol_actor_fn)(ol_actor_t *actor, void *arg);

typedef struct ol_actor_opts_s {
  const char *name;
  size_t      mailbox_capacity;
  int         supervise;
} ol_actor_opts_t;

ol_actor_t* ol_actor_spawn(ol_loop_t *loop, ol_actor_fn fn, void *arg, const ol_actor_opts_t *opts);
int         ol_actor_send(ol_actor_t *actor, void *msg);
int         ol_actor_stop(ol_actor_t *actor);

/* ================================= Parallel ================================= */

typedef struct ol_parallel_pool_s ol_parallel_pool_t;
typedef void (*ol_work_fn)(void *arg);

typedef struct ol_parallel_opts_s {
  int threads;
  int affinity; /* 0 none, 1 compact, 2 scatter */
} ol_parallel_opts_t;

ol_parallel_pool_t* ol_parallel_pool_create(const ol_parallel_opts_t *opts);
int                 ol_parallel_submit(ol_parallel_pool_t *p, ol_work_fn fn, void *arg);
int                 ol_parallel_shutdown(ol_parallel_pool_t *p);

/* ============================== Observability =============================== */

typedef void (*ol_trace_hook_fn)(const char *phase, const char *name, uint64_t dur_ms, int status);
void ol_set_trace_hook(ol_trace_hook_fn fn);

typedef void (*ol_metric_hook_fn)(const char *key, double value);
void ol_set_metric_hook(ol_metric_hook_fn fn);

/* ===================== Internal/extension context hooks ===================== */

typedef struct ol_ctx_s {
  int async_mode;    /* 0 sync/await, 1 async, 2 actor, 3 parallel */
  int event_loop;    /* 0 off, 1 on */
  int max_workers;   /* parallel threads override */
  int debug;
} ol_ctx_t;

void ol_ctx_set(const ol_ctx_t *ctx);
const ol_ctx_t* ol_ctx_get(void);

#ifdef __cplusplus
}
#endif

#endif /* OLSRT_H */
