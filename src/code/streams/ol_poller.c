#include "ol_poller.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

/* Backend selection */
#if defined(__linux__)
  #define OL_BACKEND_EPOLL 1
  #include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  #define OL_BACKEND_KQUEUE 1
  #include <sys/types.h>
  #include <sys/event.h>
  #include <sys/time.h>
#else
  #define OL_BACKEND_SELECT 1
  #include <sys/select.h>
#endif

struct ol_poller {
#if defined(OL_BACKEND_EPOLL)
    int epfd;
    struct epoll_event *evs;
    int capacity;
#elif defined(OL_BACKEND_KQUEUE)
    int kq;
    struct kevent *evs;
    int capacity;
#else
    fd_set rfds;
    fd_set wfds;
    fd_set efds;
    int    maxfd;
    /* simple registry for tags/masks by fd */
    uint64_t *tags;   // index by fd
    uint32_t *masks;  // index by fd
    int       alloc;  // allocated size
#endif
};

/* Common helpers */
static uint32_t ol_mask_to_backend(uint32_t mask) {
#if defined(OL_BACKEND_EPOLL)
    uint32_t m = 0;
    if (mask & OL_POLL_IN)  m |= EPOLLIN;
    if (mask & OL_POLL_OUT) m |= EPOLLOUT;
    if (mask & OL_POLL_ERR) m |= EPOLLERR | EPOLLHUP;
    return m;
#elif defined(OL_BACKEND_KQUEUE)
    /* kqueue uses EVFILT_READ/EVFILT_WRITE; errors arrive via flags. */
    (void)mask; // mapping is handled per filter add
    return mask;
#else
    return mask; // select uses FD sets directly
#endif
}

static uint32_t ol_backend_to_mask_epoll(uint32_t events) {
    uint32_t m = 0;
    if (events & EPOLLIN)  m |= OL_POLL_IN;
    if (events & EPOLLOUT) m |= OL_POLL_OUT;
    if (events & (EPOLLERR | EPOLLHUP)) m |= OL_POLL_ERR;
    return m;
}

/* Create */
ol_poller_t* ol_poller_create(void) {
    ol_poller_t *p = (ol_poller_t*)calloc(1, sizeof(ol_poller_t));
    if (!p) return NULL;

#if defined(OL_BACKEND_EPOLL)
    p->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (p->epfd < 0) { free(p); return NULL; }
    p->capacity = 64;
    p->evs = (struct epoll_event*)malloc(sizeof(struct epoll_event) * p->capacity);
    if (!p->evs) { close(p->epfd); free(p); return NULL; }

#elif defined(OL_BACKEND_KQUEUE)
    p->kq = kqueue();
    if (p->kq < 0) { free(p); return NULL; }
    p->capacity = 64;
    p->evs = (struct kevent*)malloc(sizeof(struct kevent) * p->capacity);
    if (!p->evs) { close(p->kq); free(p); return NULL; }

#else
    FD_ZERO(&p->rfds);
    FD_ZERO(&p->wfds);
    FD_ZERO(&p->efds);
    p->maxfd = -1;
    p->alloc = 1024;
    p->tags  = (uint64_t*)calloc(p->alloc, sizeof(uint64_t));
    p->masks = (uint32_t*)calloc(p->alloc, sizeof(uint32_t));
    if (!p->tags || !p->masks) { free(p->tags); free(p->masks); free(p); return NULL; }
#endif

    return p;
}

/* Destroy */
void ol_poller_destroy(ol_poller_t *p) {
    if (!p) return;
#if defined(OL_BACKEND_EPOLL)
    if (p->evs) free(p->evs);
    if (p->epfd >= 0) close(p->epfd);
#elif defined(OL_BACKEND_KQUEUE)
    if (p->evs) free(p->evs);
    if (p->kq >= 0) close(p->kq);
#else
    free(p->tags);
    free(p->masks);
#endif
    free(p);
}

/* Add */
int ol_poller_add(ol_poller_t *p, int fd, uint32_t mask, uint64_t tag) {
    if (!p || fd < 0) return -1;
#if defined(OL_BACKEND_EPOLL)
    struct epoll_event ev;
    ev.events = ol_mask_to_backend(mask);
    ev.data.u64 = tag;
    return epoll_ctl(p->epfd, EPOLL_CTL_ADD, fd, &ev);

#elif defined(OL_BACKEND_KQUEUE)
    struct kevent changes[2];
    int n = 0;
    if (mask & OL_POLL_IN) {
        EV_SET(&changes[n++], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, (void*)(uintptr_t)tag);
    }
    if (mask & OL_POLL_OUT) {
        EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, (void*)(uintptr_t)tag);
    }
    return kevent(p->kq, changes, n, NULL, 0, NULL);

#else
    if (fd >= p->alloc) {
        int new_alloc = fd + 1024;
        uint64_t *new_tags = (uint64_t*)realloc(p->tags, sizeof(uint64_t) * new_alloc);
        uint32_t *new_masks = (uint32_t*)realloc(p->masks, sizeof(uint32_t) * new_alloc);
        if (!new_tags || !new_masks) return -1;
        /* zero initialize extended part */
        memset(new_tags + p->alloc, 0, sizeof(uint64_t) * (new_alloc - p->alloc));
        memset(new_masks + p->alloc, 0, sizeof(uint32_t) * (new_alloc - p->alloc));
        p->tags = new_tags;
        p->masks = new_masks;
        p->alloc = new_alloc;
    }
    p->tags[fd] = tag;
    p->masks[fd] = mask;
    if (mask & OL_POLL_IN)  FD_SET(fd, &p->rfds);
    if (mask & OL_POLL_OUT) FD_SET(fd, &p->wfds);
    FD_SET(fd, &p->efds);
    if (fd > p->maxfd) p->maxfd = fd;
    return 0;
#endif
}

/* Mod */
int ol_poller_mod(ol_poller_t *p, int fd, uint32_t mask, uint64_t tag) {
    if (!p || fd < 0) return -1;
#if defined(OL_BACKEND_EPOLL)
    struct epoll_event ev;
    ev.events = ol_mask_to_backend(mask);
    ev.data.u64 = tag;
    return epoll_ctl(p->epfd, EPOLL_CTL_MOD, fd, &ev);

#elif defined(OL_BACKEND_KQUEUE)
    /* In kqueue, re-issue adds with EV_ENABLE to update interest. */
    struct kevent changes[2];
    int n = 0;
    /* First disable both, then enable as needed */
    EV_SET(&changes[n++], fd, EVFILT_READ,  EV_DELETE, 0, 0, NULL);
    EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(p->kq, changes, n, NULL, 0, NULL);
    n = 0;
    if (mask & OL_POLL_IN)
        EV_SET(&changes[n++], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, (void*)(uintptr_t)tag);
    if (mask & OL_POLL_OUT)
        EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, (void*)(uintptr_t)tag);
    return kevent(p->kq, changes, n, NULL, 0, NULL);

#else
    if (fd >= p->alloc) return -1;
    p->tags[fd] = tag;
    p->masks[fd] = mask;
    FD_CLR(fd, &p->rfds);
    FD_CLR(fd, &p->wfds);
    /* efds always tracked for errors */
    FD_SET(fd, &p->efds);
    if (mask & OL_POLL_IN)  FD_SET(fd, &p->rfds);
    if (mask & OL_POLL_OUT) FD_SET(fd, &p->wfds);
    return 0;
#endif
}

/* Del */
int ol_poller_del(ol_poller_t *p, int fd) {
    if (!p || fd < 0) return -1;
#if defined(OL_BACKEND_EPOLL)
    return epoll_ctl(p->epfd, EPOLL_CTL_DEL, fd, NULL);

#elif defined(OL_BACKEND_KQUEUE)
    struct kevent changes[2];
    int n = 0;
    EV_SET(&changes[n++], fd, EVFILT_READ,  EV_DELETE, 0, 0, NULL);
    EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    return kevent(p->kq, changes, n, NULL, 0, NULL);

#else
    if (fd >= p->alloc) return -1;
    uint32_t mask = p->masks[fd];
    if (mask & OL_POLL_IN)  FD_CLR(fd, &p->rfds);
    if (mask & OL_POLL_OUT) FD_CLR(fd, &p->wfds);
    FD_CLR(fd, &p->efds);
    p->masks[fd] = 0;
    p->tags[fd]  = 0;
    if (fd == p->maxfd) {
        int m = fd - 1;
        while (m >= 0) {
            if (p->masks[m]) { p->maxfd = m; break; }
            m--;
        }
        if (m < 0) p->maxfd = -1;
    }
    return 0;
#endif
}

/* Wait */
int ol_poller_wait(ol_poller_t *p, ol_deadline_t dl, ol_poll_event_t *out, int cap) {
    if (!p || !out || cap <= 0) return -1;

#if defined(OL_BACKEND_EPOLL)
    int timeout_ms = (dl.when_ns == 0) ? -1 : ol_clamp_poll_timeout_ms(ol_deadline_remaining_ms(dl));
    if (p->capacity < cap) {
        struct epoll_event *new_evs = (struct epoll_event*)realloc(p->evs, sizeof(struct epoll_event) * cap);
        if (new_evs) { p->evs = new_evs; p->capacity = cap; }
    }
    int n = epoll_wait(p->epfd, p->evs, cap, timeout_ms);
    if (n <= 0) return n; // 0 timeout, -1 error
    for (int i = 0; i < n; i++) {
        out[i].fd   = -1; /* fd is not stored in epoll user data; require upper layer to map via tag if needed */
        out[i].mask = ol_backend_to_mask_epoll(p->evs[i].events);
        out[i].tag  = p->evs[i].data.u64;
    }
    return n;

#elif defined(OL_BACKEND_KQUEUE)
    struct timespec ts, *pts = NULL;
    if (dl.when_ns != 0) {
        int64_t rem = ol_deadline_remaining_ns(dl);
        ts.tv_sec  = rem / 1000000000LL;
        ts.tv_nsec = rem % 1000000000LL;
        pts = &ts;
    }
    if (p->capacity < cap) {
        struct kevent *new_evs = (struct kevent*)realloc(p->evs, sizeof(struct kevent) * cap);
        if (new_evs) { p->evs = new_evs; p->capacity = cap; }
    }
    int n = kevent(p->kq, NULL, 0, p->evs, cap, pts);
    if (n <= 0) return n;
    for (int i = 0; i < n; i++) {
        out[i].fd   = (int)p->evs[i].ident;
        out[i].tag  = (uint64_t)(uintptr_t)p->evs[i].udata;
        uint32_t mask = 0;
        if (p->evs[i].filter == EVFILT_READ)  mask |= OL_POLL_IN;
        if (p->evs[i].filter == EVFILT_WRITE) mask |= OL_POLL_OUT;
        if (p->evs[i].flags & (EV_ERROR | EV_EOF)) mask |= OL_POLL_ERR;
        out[i].mask = mask;
    }
    return n;

#else
    fd_set rf = p->rfds, wf = p->wfds, ef = p->efds;
    struct timeval tv, *ptv = NULL;
    if (dl.when_ns != 0) {
        int64_t rem_ms = ol_deadline_remaining_ms(dl);
        tv.tv_sec  = rem_ms / 1000;
        tv.tv_usec = (rem_ms % 1000) * 1000;
        ptv = &tv;
    }
    int n = select(p->maxfd + 1, &rf, &wf, &ef, ptv);
    if (n <= 0) return n;

    int count = 0;
    for (int fd = 0; fd <= p->maxfd && count < cap; fd++) {
        if (!p->masks[fd]) continue;
        uint32_t mask = 0;
        if (FD_ISSET(fd, &rf)) mask |= OL_POLL_IN;
        if (FD_ISSET(fd, &wf)) mask |= OL_POLL_OUT;
        if (FD_ISSET(fd, &ef)) mask |= OL_POLL_ERR;
        if (mask) {
            out[count].fd = fd;
            out[count].mask = mask;
            out[count].tag = p->tags[fd];
            count++;
        }
    }
    return count;
#endif
}
