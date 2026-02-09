/**
 * @file ol_poller.c
 * @brief Cross-platform I/O polling implementation
 * @version 1.2.0
 * 
 * This module implements a uniform polling API across different platforms:
 * - Linux: epoll
 * - macOS/BSD: kqueue
 * - Fallback: select
 */

#include "ol_poller.h"
#include "ol_common.h"
#include "ol_deadlines.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(OL_PLATFORM_WINDOWS)
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <fcntl.h>
#endif

/* --------------------------------------------------------------------------
 * Platform-specific backend selection
 * -------------------------------------------------------------------------- */

#if defined(__linux__)
    #define OL_BACKEND_EPOLL 1
    #include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__) || defined(OL_PLATFORM_BSD)
    #define OL_BACKEND_KQUEUE 1
    #include <sys/types.h>
    #include <sys/event.h>
    #include <sys/time.h>
#else
    /* Fallback to select for other platforms (including Windows) */
    #define OL_BACKEND_SELECT 1
    #if defined(OL_PLATFORM_WINDOWS)
        #include <winsock2.h>
    #else
        #include <sys/select.h>
    #endif
#endif

/* --------------------------------------------------------------------------
 * Poller structure definition
 * -------------------------------------------------------------------------- */

struct ol_poller {
#if defined(OL_BACKEND_EPOLL)
    int epfd;                       /**< epoll file descriptor */
    struct epoll_event *events;     /**< Event buffer */
    int capacity;                   /**< Buffer capacity */
#elif defined(OL_BACKEND_KQUEUE)
    int kq;                         /**< kqueue file descriptor */
    struct kevent *events;          /**< Event buffer */
    int capacity;                   /**< Buffer capacity */
#else /* SELECT backend */
    fd_set rfds;                    /**< Read set */
    fd_set wfds;                    /**< Write set */
    fd_set efds;                    /**< Error set */
    int max_fd;                     /**< Maximum fd in sets */
    
    /* Registry for fd metadata */
    uint64_t *tags;                 /**< Array of tags indexed by fd */
    uint32_t *masks;                /**< Array of masks indexed by fd */
    int alloc_size;                 /**< Allocated array size */
#endif
    
    bool initialized;               /**< Whether poller is initialized */
};

/* --------------------------------------------------------------------------
 * Helper functions
 * -------------------------------------------------------------------------- */

/**
 * @brief Convert OLSRT poll mask to backend-specific mask
 */
static uint32_t ol_mask_to_backend(uint32_t mask) {
#if defined(OL_BACKEND_EPOLL)
    uint32_t m = 0;
    if (mask & OL_POLL_IN)  m |= EPOLLIN;
    if (mask & OL_POLL_OUT) m |= EPOLLOUT;
    if (mask & OL_POLL_ERR) m |= EPOLLERR | EPOLLHUP;
    return m;
#elif defined(OL_BACKEND_KQUEUE)
    /* kqueue mapping done per filter */
    return mask;
#else
    return mask;
#endif
}

/**
 * @brief Convert backend-specific events to OLSRT mask
 */
static uint32_t ol_backend_to_mask(uint32_t backend_events) {
#if defined(OL_BACKEND_EPOLL)
    uint32_t m = 0;
    if (backend_events & EPOLLIN)  m |= OL_POLL_IN;
    if (backend_events & EPOLLOUT) m |= OL_POLL_OUT;
    if (backend_events & (EPOLLERR | EPOLLHUP)) m |= OL_POLL_ERR;
    return m;
#elif defined(OL_BACKEND_KQUEUE)
    /* Handled in kqueue-specific code */
    return backend_events;
#else
    return backend_events;
#endif
}

/* --------------------------------------------------------------------------
 * Public API implementation
 * -------------------------------------------------------------------------- */

ol_poller_t* ol_poller_create(void) {
    ol_poller_t *p = (ol_poller_t*)calloc(1, sizeof(ol_poller_t));
    if (!p) {
        return NULL;
    }
    
    p->initialized = false;
    
#if defined(OL_BACKEND_EPOLL)
    p->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (p->epfd < 0) {
        free(p);
        return NULL;
    }
    
    p->capacity = 64;
    p->events = (struct epoll_event*)malloc(
        sizeof(struct epoll_event) * p->capacity);
    if (!p->events) {
        close(p->epfd);
        free(p);
        return NULL;
    }
    
#elif defined(OL_BACKEND_KQUEUE)
    p->kq = kqueue();
    if (p->kq < 0) {
        free(p);
        return NULL;
    }
    
    p->capacity = 64;
    p->events = (struct kevent*)malloc(
        sizeof(struct kevent) * p->capacity);
    if (!p->events) {
        close(p->kq);
        free(p);
        return NULL;
    }
    
#else /* SELECT backend */
    
#if defined(OL_PLATFORM_WINDOWS)
    /* Initialize Winsock on Windows */
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        free(p);
        return NULL;
    }
#endif
    
    FD_ZERO(&p->rfds);
    FD_ZERO(&p->wfds);
    FD_ZERO(&p->efds);
    p->max_fd = -1;
    
    p->alloc_size = 1024;
    p->tags = (uint64_t*)calloc(p->alloc_size, sizeof(uint64_t));
    p->masks = (uint32_t*)calloc(p->alloc_size, sizeof(uint32_t));
    
    if (!p->tags || !p->masks) {
        free(p->tags);
        free(p->masks);
#if defined(OL_PLATFORM_WINDOWS)
        WSACleanup();
#endif
        free(p);
        return NULL;
    }
    
#endif /* Backend selection */
    
    p->initialized = true;
    return p;
}

void ol_poller_destroy(ol_poller_t *p) {
    if (!p) {
        return;
    }
    
#if defined(OL_BACKEND_EPOLL)
    if (p->events) {
        free(p->events);
    }
    if (p->epfd >= 0) {
        close(p->epfd);
    }
#elif defined(OL_BACKEND_KQUEUE)
    if (p->events) {
        free(p->events);
    }
    if (p->kq >= 0) {
        close(p->kq);
    }
#else /* SELECT backend */
    free(p->tags);
    free(p->masks);
    
#if defined(OL_PLATFORM_WINDOWS)
    WSACleanup();
#endif
    
#endif /* Backend selection */
    
    free(p);
}

int ol_poller_add(ol_poller_t *p, int fd, uint32_t mask, uint64_t tag) {
    if (!p || fd < 0) {
        return OL_ERROR;
    }
    
#if defined(OL_BACKEND_EPOLL)
    struct epoll_event ev;
    ev.events = ol_mask_to_backend(mask);
    ev.data.u64 = tag;
    
    if (epoll_ctl(p->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        return OL_ERROR;
    }
    
    return OL_SUCCESS;
    
#elif defined(OL_BACKEND_KQUEUE)
    struct kevent changes[2];
    int n_changes = 0;
    
    if (mask & OL_POLL_IN) {
        EV_SET(&changes[n_changes++], fd, EVFILT_READ,
               EV_ADD | EV_ENABLE, 0, 0, (void*)(uintptr_t)tag);
    }
    
    if (mask & OL_POLL_OUT) {
        EV_SET(&changes[n_changes++], fd, EVFILT_WRITE,
               EV_ADD | EV_ENABLE, 0, 0, (void*)(uintptr_t)tag);
    }
    
    if (n_changes == 0) {
        return OL_ERROR; /* No events to monitor */
    }
    
    if (kevent(p->kq, changes, n_changes, NULL, 0, NULL) < 0) {
        return OL_ERROR;
    }
    
    return OL_SUCCESS;
    
#else /* SELECT backend */
    
    /* Ensure fd fits in our arrays */
    if (fd >= p->alloc_size) {
        int new_size = fd + 1024;
        uint64_t *new_tags = (uint64_t*)realloc(p->tags,
            sizeof(uint64_t) * new_size);
        uint32_t *new_masks = (uint32_t*)realloc(p->masks,
            sizeof(uint32_t) * new_size);
        
        if (!new_tags || !new_masks) {
            free(new_tags);
            free(new_masks);
            return OL_ERROR;
        }
        
        /* Initialize new entries */
        memset(new_tags + p->alloc_size, 0,
            sizeof(uint64_t) * (new_size - p->alloc_size));
        memset(new_masks + p->alloc_size, 0,
            sizeof(uint32_t) * (new_size - p->alloc_size));
        
        p->tags = new_tags;
        p->masks = new_masks;
        p->alloc_size = new_size;
    }
    
    /* Update metadata */
    p->tags[fd] = tag;
    p->masks[fd] = mask;
    
    /* Update fd_sets */
    if (mask & OL_POLL_IN) {
        FD_SET(fd, &p->rfds);
    } else {
        FD_CLR(fd, &p->rfds);
    }
    
    if (mask & OL_POLL_OUT) {
        FD_SET(fd, &p->wfds);
    } else {
        FD_CLR(fd, &p->wfds);
    }
    
    /* Always monitor for errors */
    FD_SET(fd, &p->efds);
    
    /* Update max_fd */
    if (fd > p->max_fd) {
        p->max_fd = fd;
    }
    
    return OL_SUCCESS;
    
#endif /* Backend selection */
}

int ol_poller_mod(ol_poller_t *p, int fd, uint32_t mask, uint64_t tag) {
    if (!p || fd < 0) {
        return OL_ERROR;
    }
    
#if defined(OL_BACKEND_EPOLL)
    struct epoll_event ev;
    ev.events = ol_mask_to_backend(mask);
    ev.data.u64 = tag;
    
    if (epoll_ctl(p->epfd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        return OL_ERROR;
    }
    
    return OL_SUCCESS;
    
#elif defined(OL_BACKEND_KQUEUE)
    /* kqueue requires deleting and re-adding */
    struct kevent changes[4];
    int n_changes = 0;
    
    /* Delete existing filters */
    EV_SET(&changes[n_changes++], fd, EVFILT_READ,
           EV_DELETE, 0, 0, NULL);
    EV_SET(&changes[n_changes++], fd, EVFILT_WRITE,
           EV_DELETE, 0, 0, NULL);
    
    /* Ignore errors on delete (fd might not be registered) */
    kevent(p->kq, changes, n_changes, NULL, 0, NULL);
    
    /* Re-add with new mask */
    n_changes = 0;
    if (mask & OL_POLL_IN) {
        EV_SET(&changes[n_changes++], fd, EVFILT_READ,
               EV_ADD | EV_ENABLE, 0, 0, (void*)(uintptr_t)tag);
    }
    
    if (mask & OL_POLL_OUT) {
        EV_SET(&changes[n_changes++], fd, EVFILT_WRITE,
               EV_ADD | EV_ENABLE, 0, 0, (void*)(uintptr_t)tag);
    }
    
    if (n_changes == 0) {
        return OL_SUCCESS; /* Nothing to monitor */
    }
    
    if (kevent(p->kq, changes, n_changes, NULL, 0, NULL) < 0) {
        return OL_ERROR;
    }
    
    return OL_SUCCESS;
    
#else /* SELECT backend */
    
    /* Ensure fd is within bounds */
    if (fd >= p->alloc_size) {
        return OL_ERROR;
    }
    
    /* Update metadata */
    p->tags[fd] = tag;
    p->masks[fd] = mask;
    
    /* Update fd_sets */
    if (mask & OL_POLL_IN) {
        FD_SET(fd, &p->rfds);
    } else {
        FD_CLR(fd, &p->rfds);
    }
    
    if (mask & OL_POLL_OUT) {
        FD_SET(fd, &p->wfds);
    } else {
        FD_CLR(fd, &p->wfds);
    }
    
    /* Always monitor for errors */
    FD_SET(fd, &p->efds);
    
    return OL_SUCCESS;
    
#endif /* Backend selection */
}

int ol_poller_del(ol_poller_t *p, int fd) {
    if (!p || fd < 0) {
        return OL_ERROR;
    }
    
#if defined(OL_BACKEND_EPOLL)
    /* epoll_ctl with EPOLL_CTL_DEL and NULL event */
    if (epoll_ctl(p->epfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        /* Ignore error if fd wasn't registered */
        if (errno == ENOENT) {
            return OL_SUCCESS;
        }
        return OL_ERROR;
    }
    
    return OL_SUCCESS;
    
#elif defined(OL_BACKEND_KQUEUE)
    struct kevent changes[2];
    int n_changes = 0;
    
    EV_SET(&changes[n_changes++], fd, EVFILT_READ,
           EV_DELETE, 0, 0, NULL);
    EV_SET(&changes[n_changes++], fd, EVFILT_WRITE,
           EV_DELETE, 0, 0, NULL);
    
    /* Ignore errors (fd might not be registered) */
    kevent(p->kq, changes, n_changes, NULL, 0, NULL);
    
    return OL_SUCCESS;
    
#else /* SELECT backend */
    
    if (fd >= p->alloc_size) {
        return OL_SUCCESS; /* Not registered */
    }
    
    /* Clear from fd_sets */
    FD_CLR(fd, &p->rfds);
    FD_CLR(fd, &p->wfds);
    FD_CLR(fd, &p->efds);
    
    /* Clear metadata */
    p->tags[fd] = 0;
    p->masks[fd] = 0;
    
    /* Update max_fd if needed */
    if (fd == p->max_fd) {
        /* Find new maximum */
        int new_max = -1;
        for (int i = p->max_fd - 1; i >= 0; i--) {
            if (p->masks[i] != 0) {
                new_max = i;
                break;
            }
        }
        p->max_fd = new_max;
    }
    
    return OL_SUCCESS;
    
#endif /* Backend selection */
}

int ol_poller_wait(ol_poller_t *p,
                   ol_deadline_t dl,
                   ol_poll_event_t *out,
                   int cap) {
    if (!p || !out || cap <= 0) {
        return OL_ERROR;
    }
    
#if defined(OL_BACKEND_EPOLL)
    
    /* Calculate timeout */
    int timeout_ms = -1; /* Infinite by default */
    if (dl.when_ns != 0) {
        int64_t remaining_ms = ol_deadline_remaining_ms(dl);
        timeout_ms = ol_clamp_poll_timeout_ms(remaining_ms);
    }
    
    /* Ensure buffer is large enough */
    if (p->capacity < cap) {
        int new_cap = cap;
        struct epoll_event *new_events = (struct epoll_event*)realloc(
            p->events, sizeof(struct epoll_event) * new_cap);
        if (!new_events) {
            return OL_ERROR;
        }
        p->events = new_events;
        p->capacity = new_cap;
    }
    
    /* Wait for events */
    int n = epoll_wait(p->epfd, p->events, cap, timeout_ms);
    if (n < 0) {
        if (errno == EINTR) {
            return 0; /* Interrupted by signal */
        }
        return OL_ERROR;
    }
    
    /* Convert events */
    for (int i = 0; i < n; i++) {
        out[i].fd = -1; /* epoll doesn't provide fd in event */
        out[i].mask = ol_backend_to_mask(p->events[i].events);
        out[i].tag = p->events[i].data.u64;
    }
    
    return n;
    
#elif defined(OL_BACKEND_KQUEUE)
    
    /* Calculate timeout */
    struct timespec ts, *pts = NULL;
    if (dl.when_ns != 0) {
        int64_t remaining_ns = ol_deadline_remaining_ns(dl);
        ts.tv_sec = remaining_ns / 1000000000LL;
        ts.tv_nsec = remaining_ns % 1000000000LL;
        pts = &ts;
    }
    
    /* Ensure buffer is large enough */
    if (p->capacity < cap) {
        int new_cap = cap;
        struct kevent *new_events = (struct kevent*)realloc(
            p->events, sizeof(struct kevent) * new_cap);
        if (!new_events) {
            return OL_ERROR;
        }
        p->events = new_events;
        p->capacity = new_cap;
    }
    
    /* Wait for events */
    int n = kevent(p->kq, NULL, 0, p->events, cap, pts);
    if (n < 0) {
        if (errno == EINTR) {
            return 0; /* Interrupted by signal */
        }
        return OL_ERROR;
    }
    
    /* Convert events */
    for (int i = 0; i < n; i++) {
        out[i].fd = (int)p->events[i].ident;
        out[i].tag = (uint64_t)(uintptr_t)p->events[i].udata;
        
        uint32_t mask = 0;
        if (p->events[i].filter == EVFILT_READ) {
            mask |= OL_POLL_IN;
        }
        if (p->events[i].filter == EVFILT_WRITE) {
            mask |= OL_POLL_OUT;
        }
        if (p->events[i].flags & (EV_ERROR | EV_EOF)) {
            mask |= OL_POLL_ERR;
        }
        
        out[i].mask = mask;
    }
    
    return n;
    
#else /* SELECT backend */
    
    /* Prepare fd_sets */
    fd_set rfds = p->rfds;
    fd_set wfds = p->wfds;
    fd_set efds = p->efds;
    
    /* Calculate timeout */
    struct timeval tv, *ptv = NULL;
    if (dl.when_ns != 0) {
        int64_t remaining_ms = ol_deadline_remaining_ms(dl);
        tv.tv_sec = remaining_ms / 1000;
        tv.tv_usec = (remaining_ms % 1000) * 1000;
        ptv = &tv;
    }
    
    /* Wait for events */
    int n;
#if defined(OL_PLATFORM_WINDOWS)
    if (p->max_fd == -1) {
        /* No fds to wait on, just sleep for timeout */
        if (ptv) {
            Sleep((DWORD)(tv.tv_sec * 1000 + tv.tv_usec / 1000));
        }
        return 0;
    }
    n = select(p->max_fd + 1, &rfds, &wfds, &efds, ptv);
#else
    n = select(p->max_fd + 1, &rfds, &wfds, &efds, ptv);
#endif
    
    if (n < 0) {
#if defined(OL_PLATFORM_WINDOWS)
        int err = WSAGetLastError();
        if (err == WSAEINTR) {
            return 0;
        }
#else
        if (errno == EINTR) {
            return 0;
        }
#endif
        return OL_ERROR;
    }
    
    if (n == 0) {
        return 0; /* Timeout */
    }
    
    /* Convert events */
    int count = 0;
    for (int fd = 0; fd <= p->max_fd && count < cap; fd++) {
        if (p->masks[fd] == 0) {
            continue; /* Not registered */
        }
        
        uint32_t mask = 0;
        
        if (FD_ISSET(fd, &rfds)) {
            mask |= OL_POLL_IN;
        }
        if (FD_ISSET(fd, &wfds)) {
            mask |= OL_POLL_OUT;
        }
        if (FD_ISSET(fd, &efds)) {
            mask |= OL_POLL_ERR;
        }
        
        if (mask != 0) {
            out[count].fd = fd;
            out[count].mask = mask;
            out[count].tag = p->tags[fd];
            count++;
        }
    }
    
    return count;
    
#endif /* Backend selection */
}