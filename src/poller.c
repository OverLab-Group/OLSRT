/* OLSRT - Poller backends */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>
#include <string.h>

/* Callback registry entry per file descriptor */
typedef struct ol_cbreg_s {
    ol_poller_cb cb;
    void        *arg;
    int          ev;   /* interest mask: OL_EVT_READ | OL_EVT_WRITE */
} ol_cbreg_t;

/* Poller object */
struct ol_poller_s {
    int backend;           /* 1 epoll, 2 kqueue, 3 iocp */
    int max_events;
    ol_cbreg_t *cbs;       /* fd-indexed registry (sparse, grown as needed) */
    int ccap;              /* current capacity */

#if defined(__linux__)
    int epfd;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    int kqfd;
#elif defined(_WIN32)
    HANDLE iocp;
#endif
};

/* Ensure callback registry can address fd; grow amortized */
static int ol_poller_ensure_cbcap(struct ol_poller_s *p, int fd) {
    if (fd < 0) return OL_ERR_ARG;
    if (fd < p->ccap) return OL_OK;
    int nc = fd + 1024;
    ol_cbreg_t *n = (ol_cbreg_t*)realloc(p->cbs, (size_t)nc * sizeof(ol_cbreg_t));
    if (!n) return OL_ERR_ALLOC;
    /* Zero new range */
    for (int i = p->ccap; i < nc; i++) { n[i].cb = NULL; n[i].arg = NULL; n[i].ev = 0; }
    p->cbs  = n;
    p->ccap = nc;
    return OL_OK;
}

#if defined(__linux__)

/* ================================ epoll (Linux) ============================= */

#include <sys/epoll.h>
#include <unistd.h>

static int ol_epoll_mask(int ev) {
    int m = 0;
    if (ev & OL_EVT_READ)  m |= EPOLLIN;
    if (ev & OL_EVT_WRITE) m |= EPOLLOUT;
    return m;
}

ol_poller_t* ol_poller_create(int hint, int max_events) {
    struct ol_poller_s *p = (struct ol_poller_s*)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->backend    = 1;
    p->max_events = (max_events > 0) ? max_events : 1024;
    p->ccap       = 4096;
    p->cbs        = (ol_cbreg_t*)calloc((size_t)p->ccap, sizeof(ol_cbreg_t));
    p->epfd       = epoll_create1(0);
    if (p->epfd < 0 || !p->cbs) { if (p->cbs) free(p->cbs); free(p); return NULL; }
    return (ol_poller_t*)p;
}

void ol_poller_destroy(ol_poller_t *pp) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p) return;
    close(p->epfd);
    free(p->cbs);
    free(p);
}

int ol_poller_add(ol_poller_t *pp, int fd, int events, ol_poller_cb cb, void *arg) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p || fd < 0 || !cb) return OL_ERR_STATE;
    int rc = ol_poller_ensure_cbcap(p, fd); if (rc != OL_OK) return rc;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events  = ol_epoll_mask(events);
    ev.data.fd = fd;
    if (epoll_ctl(p->epfd, EPOLL_CTL_ADD, fd, &ev) != 0) return OL_ERR_IO;
    p->cbs[fd].cb = cb; p->cbs[fd].arg = arg; p->cbs[fd].ev = events;
    return OL_OK;
}

int ol_poller_mod(ol_poller_t *pp, int fd, int events, ol_poller_cb cb, void *arg) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p || fd < 0) return OL_ERR_STATE;
    int rc = ol_poller_ensure_cbcap(p, fd); if (rc != OL_OK) return rc;
    if (cb) { p->cbs[fd].cb = cb; p->cbs[fd].arg = arg; }
    p->cbs[fd].ev = events;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events  = ol_epoll_mask(events);
    ev.data.fd = fd;
    if (epoll_ctl(p->epfd, EPOLL_CTL_MOD, fd, &ev) != 0) return OL_ERR_IO;
    return OL_OK;
}

int ol_poller_del(ol_poller_t *pp, int fd) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p || fd < 0) return OL_ERR_STATE;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    (void)epoll_ctl(p->epfd, EPOLL_CTL_DEL, fd, &ev);
    p->cbs[fd].cb = NULL; p->cbs[fd].arg = NULL; p->cbs[fd].ev = 0;
    return OL_OK;
}

int ol_poller_wait(ol_poller_t *pp, int timeout_ms) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p) return OL_ERR_STATE;
    struct epoll_event *evs = (struct epoll_event*)calloc((size_t)p->max_events, sizeof(*evs));
    if (!evs) return OL_ERR_ALLOC;
    int n = epoll_wait(p->epfd, evs, p->max_events, timeout_ms);
    if (n < 0) { free(evs); return OL_ERR_IO; }
    for (int i = 0; i < n; i++) {
        int fd = evs[i].data.fd;
        int e  = 0;
        if (evs[i].events & EPOLLIN)  e |= OL_EVT_READ;
        if (evs[i].events & EPOLLOUT) e |= OL_EVT_WRITE;
        if (evs[i].events & EPOLLERR) e |= OL_EVT_ERROR;
        if (fd >= 0 && fd < p->ccap && p->cbs[fd].cb) p->cbs[fd].cb(fd, e, p->cbs[fd].arg);
    }
    free(evs);
    return n;
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

/* ================================ kqueue (BSD/macOS) ======================== */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

ol_poller_t* ol_poller_create(int hint, int max_events) {
    struct ol_poller_s *p = (struct ol_poller_s*)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->backend    = 2;
    p->max_events = (max_events > 0) ? max_events : 1024;
    p->ccap       = 4096;
    p->cbs        = (ol_cbreg_t*)calloc((size_t)p->ccap, sizeof(ol_cbreg_t));
    p->kqfd       = kqueue();
    if (p->kqfd < 0 || !p->cbs) { if (p->cbs) free(p->cbs); free(p); return NULL; }
    return (ol_poller_t*)p;
}

void ol_poller_destroy(ol_poller_t *pp) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p) return;
    close(p->kqfd);
    free(p->cbs);
    free(p);
}

static int ol_kqueue_update(struct ol_poller_s *p, int fd, int events, int add) {
    struct kevent evs[2];
    int n = 0;
    if (events & OL_EVT_READ) {
        EV_SET(&evs[n++], fd, EVFILT_READ, add ? EV_ADD : EV_ENABLE, 0, 0, NULL);
    } else {
        EV_SET(&evs[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    }
    if (events & OL_EVT_WRITE) {
        EV_SET(&evs[n++], fd, EVFILT_WRITE, add ? EV_ADD : EV_ENABLE, 0, 0, NULL);
    } else {
        EV_SET(&evs[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    }
    int rc = kevent(p->kqfd, evs, n, NULL, 0, NULL);
    return (rc == 0) ? OL_OK : OL_ERR_IO;
}

int ol_poller_add(ol_poller_t *pp, int fd, int events, ol_poller_cb cb, void *arg) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p || fd < 0 || !cb) return OL_ERR_STATE;
    int rc = ol_poller_ensure_cbcap(p, fd); if (rc != OL_OK) return rc;
    rc = ol_kqueue_update(p, fd, events, 1);
    if (rc != OL_OK) return rc;
    p->cbs[fd].cb = cb; p->cbs[fd].arg = arg; p->cbs[fd].ev = events;
    return OL_OK;
}

int ol_poller_mod(ol_poller_t *pp, int fd, int events, ol_poller_cb cb, void *arg) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p || fd < 0) return OL_ERR_STATE;
    int rc = ol_poller_ensure_cbcap(p, fd); if (rc != OL_OK) return rc;
    if (cb) { p->cbs[fd].cb = cb; p->cbs[fd].arg = arg; }
    p->cbs[fd].ev = events;
    return ol_kqueue_update(p, fd, events, 0);
}

int ol_poller_del(ol_poller_t *pp, int fd) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p || fd < 0) return OL_ERR_STATE;
    struct kevent evs[2];
    EV_SET(&evs[0], fd, EVFILT_READ,  EV_DELETE, 0, 0, NULL);
    EV_SET(&evs[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    (void)kevent(p->kqfd, evs, 2, NULL, 0, NULL);
    p->cbs[fd].cb = NULL; p->cbs[fd].arg = NULL; p->cbs[fd].ev = 0;
    return OL_OK;
}

int ol_poller_wait(ol_poller_t *pp, int timeout_ms) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p) return OL_ERR_STATE;
    struct kevent *evs = (struct kevent*)calloc((size_t)p->max_events, sizeof(*evs));
    if (!evs) return OL_ERR_ALLOC;
    struct timespec ts, *tsp = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec  = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        tsp = &ts;
    }
    int n = kevent(p->kqfd, NULL, 0, evs, p->max_events, tsp);
    if (n < 0) { free(evs); return OL_ERR_IO; }
    for (int i = 0; i < n; i++) {
        int fd = (int)evs[i].ident;
        int e  = 0;
        if (evs[i].filter == EVFILT_READ)  e |= OL_EVT_READ;
        if (evs[i].filter == EVFILT_WRITE) e |= OL_EVT_WRITE;
        if (evs[i].flags & EV_ERROR)       e |= OL_EVT_ERROR;
        if (fd >= 0 && fd < p->ccap && p->cbs[fd].cb) p->cbs[fd].cb(fd, e, p->cbs[fd].arg);
    }
    free(evs);
    return n;
}

#elif defined(_WIN32)

/* ================================ IOCP (Windows) ============================ */

#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>

static HANDLE ol_fd_to_handle(int fd) {
    HANDLE h = INVALID_HANDLE_VALUE;
    if (fd >= 0) {
        intptr_t osfh = _get_osfhandle(fd);
        if (osfh != -1) h = (HANDLE)osfh;
        else h = (HANDLE)(intptr_t)fd; /* likely a SOCKET */
    }
    return h;
}

ol_poller_t* ol_poller_create(int hint, int max_events) {
    struct ol_poller_s *p = (struct ol_poller_s*)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->backend    = 3;
    p->max_events = (max_events > 0) ? max_events : 1024;
    p->ccap       = 4096;
    p->cbs        = (ol_cbreg_t*)calloc((size_t)p->ccap, sizeof(ol_cbreg_t));
    p->iocp       = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!p->iocp || !p->cbs) { if (p->cbs) free(p->cbs); free(p); return NULL; }
    net_init(); /* Winsock startup via compat.h */
    return (ol_poller_t*)p;
}

void ol_poller_destroy(ol_poller_t *pp) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p) return;
    CloseHandle(p->iocp);
    free(p->cbs);
    net_cleanup();
    free(p);
}

int ol_poller_add(ol_poller_t *pp, int fd, int events, ol_poller_cb cb, void *arg) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p || fd < 0 || !cb) return OL_ERR_STATE;
    int rc = ol_poller_ensure_cbcap(p, fd); if (rc != OL_OK) return rc;

    HANDLE h = ol_fd_to_handle(fd);
    if (h == INVALID_HANDLE_VALUE) return OL_ERR_ARG;
    if (!CreateIoCompletionPort(h, p->iocp, (ULONG_PTR)fd, 0)) return OL_ERR_IO;

    p->cbs[fd].cb = cb; p->cbs[fd].arg = arg; p->cbs[fd].ev = events;

    /* Kick the loop to process newly added interest */
    PostQueuedCompletionStatus(p->iocp, 0, (ULONG_PTR)fd, NULL);
    return OL_OK;
}

int ol_poller_mod(ol_poller_t *pp, int fd, int events, ol_poller_cb cb, void *arg) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p || fd < 0) return OL_ERR_STATE;
    int rc = ol_poller_ensure_cbcap(p, fd); if (rc != OL_OK) return rc;
    if (cb) { p->cbs[fd].cb = cb; p->cbs[fd].arg = arg; }
    p->cbs[fd].ev = events;
    /* Wake loop to process new interest */
    PostQueuedCompletionStatus(p->iocp, 0, (ULONG_PTR)fd, NULL);
    return OL_OK;
}

int ol_poller_del(ol_poller_t *pp, int fd) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p || fd < 0) return OL_ERR_STATE;
    p->cbs[fd].cb = NULL; p->cbs[fd].arg = NULL; p->cbs[fd].ev = 0;
    return OL_OK;
}

int ol_poller_wait(ol_poller_t *pp, int timeout_ms) {
    struct ol_poller_s *p = (struct ol_poller_s*)pp;
    if (!p) return OL_ERR_STATE;

    int processed = 0;
    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED *ov = NULL;
        BOOL ok = GetQueuedCompletionStatus(p->iocp, &bytes, &key, &ov,
                                            (timeout_ms >= 0) ? (DWORD)timeout_ms : INFINITE);
        if (!ok && ov == NULL) {
            /* Timeout or spurious wakeup with no specific operation. */
            break;
        }
        int fd = (int)key;
        if (fd >= 0 && fd < p->ccap) {
            ol_cbreg_t *r = &p->cbs[fd];
            if (r->cb) {
                /* Notify desired interest bits; IOCP is completion-driven,
                   but we minimally report the interest mask to drive state machines. */
                r->cb(fd, r->ev, r->arg);
                processed++;
            }
        }
        if (processed >= p->max_events) break;
        if (timeout_ms >= 0) break; /* finite timeout: single completion per tick */
    }
    return processed;
}

#else

/* ================================ Fallback ================================== */

ol_poller_t* ol_poller_create(int hint, int max_events) {
    (void)hint; (void)max_events;
    struct ol_poller_s *p = (struct ol_poller_s*)calloc(1, sizeof(*p));
    return (ol_poller_t*)p;
}

void ol_poller_destroy(ol_poller_t *pp) { free(pp); }

int ol_poller_add(ol_poller_t *pp, int fd, int events, ol_poller_cb cb, void *arg) {
    (void)pp; (void)fd; (void)events; (void)cb; (void)arg;
    return OL_OK;
}

int ol_poller_mod(ol_poller_t *pp, int fd, int events, ol_poller_cb cb, void *arg) {
    (void)pp; (void)fd; (void)events; (void)cb; (void)arg;
    return OL_OK;
}

int ol_poller_del(ol_poller_t *pp, int fd) {
    (void)pp; (void)fd;
    return OL_OK;
}

int ol_poller_wait(ol_poller_t *pp, int timeout_ms) {
    (void)pp;
    struct timespec ts = { timeout_ms / 1000, (timeout_ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
    return 0;
}

#endif
