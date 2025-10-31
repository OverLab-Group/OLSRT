/* OLSRT - Globals & Config */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* ============================ Feature flags ============================ */

static ol_features_t G_FEAT = {
  /* Fiber integration is extension-layer work; flip when wired */
  .fibers_available = 0,

#if defined(__linux__)
  .epoll_available = 1,
#else
  .epoll_available = 0,
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  .kqueue_available = 1,
#else
  .kqueue_available = 0,
#endif

#if defined(_WIN32)
  .iocp_available = 1,
#else
  .iocp_available = 0,
#endif

  /* Toggle when TLS engine (in-house or embedded) is compiled in */
  .tls_available  = 0,

  /* Protocol modules included in this build */
  .http_available = 1,
  .ws_available   = 1
};

const ol_features_t* ol_features(void) { return &G_FEAT; }

/* ================================ Config =================================== */

static ol_config_t G_CFG = {
  .event_loop_enabled = 0,
  .debug              = 0,
  .max_events         = 1024,
  .max_workers        = 4,
  .allow_blocking     = 1,
  .poller_hint        = 0,   /* 0 auto, 1 epoll, 2 kqueue, 3 iocp */
  .fiber_await        = 0,
  .default_timeout_ms = 30000
};

const ol_config_t* ol_config_get(void) { return &G_CFG; }

int ol_config_set(const ol_config_t *cfg) {
  if (!cfg) return OL_ERR_CONFIG;
  /* Shallow copy is fine; caller owns its cfg memory. */
  G_CFG = *cfg;
  return OL_OK;
}

/* ================================ Logging ================================== */

static ol_log_fn         G_LOG   = NULL;
static ol_trace_hook_fn  G_TRACE = NULL;
static ol_metric_hook_fn G_METRIC= NULL;

void ol_set_logger(ol_log_fn fn) { G_LOG = fn; }

/* Note: Keep this small and non-allocating except for a local buffer. */
void ol_log(int level, const char *fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (G_LOG) {
    G_LOG(level, buf);
  } else {
    fprintf(stderr, "[OL:%d] %s\n", level, buf);
  }
}

/* ============================= Observability =============================== */

void ol_set_trace_hook(ol_trace_hook_fn fn) { G_TRACE = fn; }
void ol_set_metric_hook(ol_metric_hook_fn fn) { G_METRIC = fn; }

/* Optional inline helpers to emit standardized trace/metrics safely. */
static inline void ol_trace_emit(const char *phase, const char *name, uint64_t dur_ms, int status) {
  if (G_TRACE) G_TRACE(phase, name, dur_ms, status);
}
static inline void ol_metric_emit(const char *key, double value) {
  if (G_METRIC) G_METRIC(key, value);
}

/* ========================= Extension context (thin) ========================= */

static ol_ctx_t G_CTX = { 0, 0, 4, 0 };

void ol_ctx_set(const ol_ctx_t *ctx) { if (ctx) G_CTX = *ctx; }
const ol_ctx_t* ol_ctx_get(void) { return &G_CTX; }
