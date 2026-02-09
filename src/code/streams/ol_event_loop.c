/**
 * @file ol_event_loop.c
 * @brief Portable event loop implementation with I/O and timer support
 * @version 1.2.0
 * 
 * This module implements a single-threaded event loop that works across
 * Linux, Windows, macOS, and BSD systems. It supports I/O events, timers,
 * and wake mechanisms with a uniform API.
 */

#include "ol_event_loop.h"
#include "ol_common.h"
#include "ol_deadlines.h"
#include "ol_lock_mutex.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(OL_PLATFORM_WINDOWS)
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <unistd.h>
    #include <fcntl.h>
#endif

/* --------------------------------------------------------------------------
 * Internal structures
 * -------------------------------------------------------------------------- */

/**
 * @brief Registered event entry
 */
typedef struct {
    uint64_t id;                /**< Unique event identifier */
    ol_ev_type_t type;          /**< Event type (I/O or timer) */
    
    /* I/O fields */
    int fd;                     /**< File descriptor (I/O events) */
    uint32_t mask;              /**< Poll mask (I/O events) */
    
    /* Timer fields */
    int64_t when_ns;            /**< Absolute deadline (timer events) */
    int64_t periodic_ns;        /**< Periodic interval (0 for one-shot) */
    
    /* Common fields */
    ol_event_cb callback;       /**< User callback function */
    void *user_data;            /**< User data for callback */
    
    bool active;                /**< Whether entry is active */
} ol_event_entry_t;

/**
 * @brief Event loop internal state
 */
struct ol_event_loop {
    bool running;               /**< Whether loop is running */
    bool should_stop;           /**< Stop request flag */
    
    /* Wake mechanism */
    int wake_read_fd;           /**< Read end of wake pipe */
    int wake_write_fd;          /**< Write end of wake pipe */
    
    /* Poller instance */
    ol_poller_t *poller;        /**< I/O poller backend */
    
    /* Event registry */
    ol_event_entry_t *events;   /**< Dynamic array of events */
    size_t event_count;         /**< Number of active entries */
    size_t event_capacity;      /**< Allocated capacity */
    
    /* Synchronization */
    ol_mutex_t mutex;           /**< Protects event registry */
    
    /* ID generation */
    uint64_t next_event_id;     /**< Next available event ID */
    
    /* Statistics */
    uint64_t iteration_count;   /**< Number of loop iterations */
    uint64_t event_dispatch_count; /**< Number of events dispatched */
};

/* --------------------------------------------------------------------------
 * Internal helper functions
 * -------------------------------------------------------------------------- */

/**
 * @brief Create a wake pipe (cross-platform)
 */
static int ol_create_wake_pipe(int fds[2]) {
#if defined(OL_PLATFORM_WINDOWS)
    /* Windows: use socketpair or pipe from Winsock */
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return OL_ERROR;
    }
    
    /* For simplicity, we'll use a simple self-pipe with TCP loopback */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        return OL_ERROR;
    }
    
    int addrlen = sizeof(addr);
    getsockname(sock, (struct sockaddr*)&addr, &addrlen);
    
    if (listen(sock, 1) != 0) {
        closesocket(sock);
        return OL_ERROR;
    }
    
    SOCKET write_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (write_sock == INVALID_SOCKET) {
        closesocket(sock);
        return OL_ERROR;
    }
    
    if (connect(write_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        closesocket(write_sock);
        return OL_ERROR;
    }
    
    SOCKET read_sock = accept(sock, NULL, NULL);
    if (read_sock == INVALID_SOCKET) {
        closesocket(sock);
        closesocket(write_sock);
        return OL_ERROR;
    }
    
    /* Set non-blocking mode */
    u_long mode = 1;
    ioctlsocket(read_sock, FIONBIO, &mode);
    ioctlsocket(write_sock, FIONBIO, &mode);
    
    fds[0] = read_sock;
    fds[1] = write_sock;
    closesocket(sock);
    
    return OL_SUCCESS;
#else
    /* POSIX: use pipe() */
    if (pipe(fds) != 0) {
        return OL_ERROR;
    }
    
    /* Set non-blocking mode */
    int flags;
    flags = fcntl(fds[0], F_GETFL, 0);
    fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
    
    flags = fcntl(fds[1], F_GETFL, 0);
    fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);
    
    return OL_SUCCESS;
#endif
}

/**
 * @brief Close a file descriptor (cross-platform)
 */
static void ol_close_fd(int fd) {
    if (fd >= 0) {
#if defined(OL_PLATFORM_WINDOWS)
        closesocket(fd);
#else
        close(fd);
#endif
    }
}

/**
 * @brief Drain wake pipe
 */
static void ol_drain_wake_pipe(int fd) {
    char buffer[256];
#if defined(OL_PLATFORM_WINDOWS)
    while (recv(fd, buffer, sizeof(buffer), 0) > 0) {
        /* Keep draining */
    }
#else
    while (read(fd, buffer, sizeof(buffer)) > 0) {
        /* Keep draining */
    }
#endif
}

/**
 * @brief Find event by ID
 */
static ol_event_entry_t* ol_find_event(ol_event_loop_t *loop, uint64_t id) {
    if (!loop || id == 0) {
        return NULL;
    }
    
    for (size_t i = 0; i < loop->event_count; i++) {
        if (loop->events[i].active && loop->events[i].id == id) {
            return &loop->events[i];
        }
    }
    
    return NULL;
}

/**
 * @brief Ensure event array has enough capacity
 */
static int ol_ensure_event_capacity(ol_event_loop_t *loop, size_t min_capacity) {
    if (min_capacity <= loop->event_capacity) {
        return OL_SUCCESS;
    }
    
    size_t new_capacity = loop->event_capacity * 2;
    if (new_capacity < min_capacity) {
        new_capacity = min_capacity;
    }
    if (new_capacity < 16) {
        new_capacity = 16;
    }
    
    ol_event_entry_t *new_events = (ol_event_entry_t*)realloc(
        loop->events, sizeof(ol_event_entry_t) * new_capacity);
    if (!new_events) {
        return OL_ERROR;
    }
    
    /* Initialize new entries */
    for (size_t i = loop->event_capacity; i < new_capacity; i++) {
        memset(&new_events[i], 0, sizeof(ol_event_entry_t));
    }
    
    loop->events = new_events;
    loop->event_capacity = new_capacity;
    
    return OL_SUCCESS;
}

/**
 * @brief Compact event array (remove inactive entries)
 */
static void ol_compact_events(ol_event_loop_t *loop) {
    size_t write_index = 0;
    
    for (size_t read_index = 0; read_index < loop->event_count; read_index++) {
        if (loop->events[read_index].active) {
            if (write_index != read_index) {
                loop->events[write_index] = loop->events[read_index];
            }
            write_index++;
        }
    }
    
    loop->event_count = write_index;
    
    /* Shrink array if it's mostly empty */
    if (loop->event_capacity > 64 && loop->event_count < loop->event_capacity / 4) {
        size_t new_capacity = loop->event_capacity / 2;
        ol_event_entry_t *new_events = (ol_event_entry_t*)realloc(
            loop->events, sizeof(ol_event_entry_t) * new_capacity);
        if (new_events) {
            loop->events = new_events;
            loop->event_capacity = new_capacity;
        }
    }
}

/**
 * @brief Calculate next timer deadline
 */
static int64_t ol_next_timer_deadline(ol_event_loop_t *loop) {
    int64_t next_deadline = 0; /* 0 means no timers */
    
    for (size_t i = 0; i < loop->event_count; i++) {
        ol_event_entry_t *entry = &loop->events[i];
        if (!entry->active || entry->type != OL_EV_TIMER) {
            continue;
        }
        
        if (next_deadline == 0 || entry->when_ns < next_deadline) {
            next_deadline = entry->when_ns;
        }
    }
    
    return next_deadline;
}

/**
 * @brief Process expired timers
 */
static void ol_process_timers(ol_event_loop_t *loop) {
    int64_t now = ol_monotonic_now_ns();
    
    for (size_t i = 0; i < loop->event_count; i++) {
        ol_event_entry_t *entry = &loop->events[i];
        
        if (!entry->active || entry->type != OL_EV_TIMER) {
            continue;
        }
        
        if (now >= entry->when_ns) {
            /* Timer expired */
            loop->event_dispatch_count++;
            
            /* Invoke callback */
            if (entry->callback) {
                entry->callback(loop, OL_EV_TIMER, -1, entry->user_data);
            }
            
            /* Reschedule if periodic */
            if (entry->periodic_ns > 0) {
                entry->when_ns += entry->periodic_ns;
                /* Handle missed periods */
                while (now >= entry->when_ns) {
                    entry->when_ns += entry->periodic_ns;
                }
            } else {
                /* One-shot timer: mark as inactive */
                entry->active = false;
            }
        }
    }
    
    /* Compact after processing one-shot timers */
    ol_compact_events(loop);
}

/* --------------------------------------------------------------------------
 * Public API implementation
 * -------------------------------------------------------------------------- */

ol_event_loop_t* ol_event_loop_create(void) {
    ol_event_loop_t *loop = (ol_event_loop_t*)calloc(1, sizeof(ol_event_loop_t));
    if (!loop) {
        return NULL;
    }
    
    /* Initialize mutex */
    if (ol_mutex_init(&loop->mutex) != OL_SUCCESS) {
        free(loop);
        return NULL;
    }
    
    /* Create poller */
    loop->poller = ol_poller_create();
    if (!loop->poller) {
        ol_mutex_destroy(&loop->mutex);
        free(loop);
        return NULL;
    }
    
    /* Create wake pipe */
    int wake_fds[2];
    if (ol_create_wake_pipe(wake_fds) != OL_SUCCESS) {
        ol_poller_destroy(loop->poller);
        ol_mutex_destroy(&loop->mutex);
        free(loop);
        return NULL;
    }
    
    loop->wake_read_fd = wake_fds[0];
    loop->wake_write_fd = wake_fds[1];
    
    /* Register wake pipe with poller */
    if (ol_poller_add(loop->poller, loop->wake_read_fd, 
                      OL_POLL_IN, 0) != OL_SUCCESS) {
        ol_close_fd(loop->wake_read_fd);
        ol_close_fd(loop->wake_write_fd);
        ol_poller_destroy(loop->poller);
        ol_mutex_destroy(&loop->mutex);
        free(loop);
        return NULL;
    }
    
    /* Initialize event array */
    loop->event_capacity = 16;
    loop->events = (ol_event_entry_t*)calloc(
        loop->event_capacity, sizeof(ol_event_entry_t));
    if (!loop->events) {
        ol_poller_del(loop->poller, loop->wake_read_fd);
        ol_close_fd(loop->wake_read_fd);
        ol_close_fd(loop->wake_write_fd);
        ol_poller_destroy(loop->poller);
        ol_mutex_destroy(&loop->mutex);
        free(loop);
        return NULL;
    }
    
    loop->running = false;
    loop->should_stop = false;
    loop->event_count = 0;
    loop->next_event_id = 1;
    loop->iteration_count = 0;
    loop->event_dispatch_count = 0;
    
    return loop;
}

void ol_event_loop_destroy(ol_event_loop_t *loop) {
    if (!loop) {
        return;
    }
    
    /* Stop loop if running */
    if (loop->running) {
        loop->should_stop = true;
        ol_event_loop_wake(loop);
    }
    
    /* Unregister all I/O events */
    for (size_t i = 0; i < loop->event_count; i++) {
        ol_event_entry_t *entry = &loop->events[i];
        if (entry->active && entry->type == OL_EV_IO) {
            ol_poller_del(loop->poller, entry->fd);
        }
    }
    
    /* Cleanup wake pipe */
    ol_poller_del(loop->poller, loop->wake_read_fd);
    ol_close_fd(loop->wake_read_fd);
    ol_close_fd(loop->wake_write_fd);
    
    /* Destroy poller */
    ol_poller_destroy(loop->poller);
    
    /* Free event array */
    free(loop->events);
    
    /* Destroy mutex */
    ol_mutex_destroy(&loop->mutex);
    
    /* Free loop structure */
    free(loop);
}

int ol_event_loop_run(ol_event_loop_t *loop) {
    if (!loop) {
        return OL_ERROR;
    }
    
    loop->running = true;
    loop->should_stop = false;
    
    /* Pre-allocate poll event buffer */
    const int POLL_EVENT_CAPACITY = 64;
    ol_poll_event_t poll_events[POLL_EVENT_CAPACITY];
    
    while (!loop->should_stop) {
        loop->iteration_count++;
        
        /* Calculate next timer deadline */
        int64_t next_timer_ns = ol_next_timer_deadline(loop);
        ol_deadline_t deadline;
        
        if (next_timer_ns == 0) {
            /* No timers: wait indefinitely */
            deadline.when_ns = 0;
        } else {
            deadline.when_ns = next_timer_ns;
        }
        
        /* Wait for I/O events */
        int n_events = ol_poller_wait(loop->poller, deadline,
                                     poll_events, POLL_EVENT_CAPACITY);
        
        if (n_events < 0) {
            /* Error occurred */
            loop->running = false;
            return OL_ERROR;
        }
        
        /* Process I/O events */
        for (int i = 0; i < n_events; i++) {
            ol_poll_event_t *pev = &poll_events[i];
            
            /* Check for wake event (tag 0 is reserved for wake pipe) */
            if (pev->tag == 0) {
                ol_drain_wake_pipe(loop->wake_read_fd);
                continue;
            }
            
            /* Find and dispatch I/O event */
            ol_mutex_lock(&loop->mutex);
            ol_event_entry_t *entry = ol_find_event(loop, pev->tag);
            ol_mutex_unlock(&loop->mutex);
            
            if (entry && entry->active && entry->type == OL_EV_IO) {
                loop->event_dispatch_count++;
                
                if (entry->callback) {
                    entry->callback(loop, OL_EV_IO, entry->fd, entry->user_data);
                }
            }
        }
        
        /* Process timers */
        ol_mutex_lock(&loop->mutex);
        ol_process_timers(loop);
        ol_mutex_unlock(&loop->mutex);
    }
    
    loop->running = false;
    return OL_SUCCESS;
}

void ol_event_loop_stop(ol_event_loop_t *loop) {
    if (!loop) {
        return;
    }
    
    loop->should_stop = true;
    ol_event_loop_wake(loop);
}

int ol_event_loop_wake(ol_event_loop_t *loop) {
    if (!loop) {
        return OL_ERROR;
    }
    
    /* Write a byte to wake pipe */
    char byte = 1;
#if defined(OL_PLATFORM_WINDOWS)
    if (send(loop->wake_write_fd, &byte, 1, 0) != 1) {
        return OL_ERROR;
    }
#else
    if (write(loop->wake_write_fd, &byte, 1) != 1) {
        return OL_ERROR;
    }
#endif
    
    return OL_SUCCESS;
}

uint64_t ol_event_loop_register_io(ol_event_loop_t *loop,
                                   int fd,
                                   uint32_t mask,
                                   ol_event_cb cb,
                                   void *user_data) {
    if (!loop || fd < 0 || !cb) {
        return 0;
    }
    
    ol_mutex_lock(&loop->mutex);
    
    /* Ensure capacity */
    if (ol_ensure_event_capacity(loop, loop->event_count + 1) != OL_SUCCESS) {
        ol_mutex_unlock(&loop->mutex);
        return 0;
    }
    
    /* Generate ID */
    uint64_t id = loop->next_event_id++;
    if (id == 0) {
        id = loop->next_event_id++; /* Skip 0 (reserved for wake pipe) */
    }
    
    /* Add to poller */
    if (ol_poller_add(loop->poller, fd, mask, id) != OL_SUCCESS) {
        ol_mutex_unlock(&loop->mutex);
        return 0;
    }
    
    /* Create event entry */
    ol_event_entry_t *entry = &loop->events[loop->event_count++];
    memset(entry, 0, sizeof(ol_event_entry_t));
    
    entry->id = id;
    entry->type = OL_EV_IO;
    entry->fd = fd;
    entry->mask = mask;
    entry->callback = cb;
    entry->user_data = user_data;
    entry->active = true;
    
    ol_mutex_unlock(&loop->mutex);
    return id;
}

int ol_event_loop_mod_io(ol_event_loop_t *loop,
                         uint64_t id,
                         uint32_t mask) {
    if (!loop || id == 0) {
        return OL_ERROR;
    }
    
    ol_mutex_lock(&loop->mutex);
    
    ol_event_entry_t *entry = ol_find_event(loop, id);
    if (!entry || entry->type != OL_EV_IO) {
        ol_mutex_unlock(&loop->mutex);
        return OL_ERROR;
    }
    
    /* Update poller */
    if (ol_poller_mod(loop->poller, entry->fd, mask, id) != OL_SUCCESS) {
        ol_mutex_unlock(&loop->mutex);
        return OL_ERROR;
    }
    
    /* Update entry */
    entry->mask = mask;
    
    ol_mutex_unlock(&loop->mutex);
    return OL_SUCCESS;
}

uint64_t ol_event_loop_register_timer(ol_event_loop_t *loop,
                                      ol_deadline_t deadline,
                                      int64_t periodic_ns,
                                      ol_event_cb cb,
                                      void *user_data) {
    if (!loop || !cb) {
        return 0;
    }
    
    ol_mutex_lock(&loop->mutex);
    
    /* Ensure capacity */
    if (ol_ensure_event_capacity(loop, loop->event_count + 1) != OL_SUCCESS) {
        ol_mutex_unlock(&loop->mutex);
        return 0;
    }
    
    /* Generate ID */
    uint64_t id = loop->next_event_id++;
    if (id == 0) {
        id = loop->next_event_id++;
    }
    
    /* Create event entry */
    ol_event_entry_t *entry = &loop->events[loop->event_count++];
    memset(entry, 0, sizeof(ol_event_entry_t));
    
    entry->id = id;
    entry->type = OL_EV_TIMER;
    entry->fd = -1;
    entry->callback = cb;
    entry->user_data = user_data;
    entry->active = true;
    
    /* Set timer values */
    if (deadline.when_ns <= 0) {
        /* Schedule immediately */
        entry->when_ns = ol_monotonic_now_ns();
    } else {
        entry->when_ns = deadline.when_ns;
    }
    
    entry->periodic_ns = periodic_ns > 0 ? periodic_ns : 0;
    
    /* Wake loop to recalculate timer deadline */
    ol_event_loop_wake(loop);
    
    ol_mutex_unlock(&loop->mutex);
    return id;
}

int ol_event_loop_unregister(ol_event_loop_t *loop, uint64_t id) {
    if (!loop || id == 0) {
        return OL_ERROR;
    }
    
    ol_mutex_lock(&loop->mutex);
    
    ol_event_entry_t *entry = ol_find_event(loop, id);
    if (!entry) {
        ol_mutex_unlock(&loop->mutex);
        return OL_ERROR;
    }
    
    /* Remove from poller if I/O event */
    if (entry->type == OL_EV_IO) {
        ol_poller_del(loop->poller, entry->fd);
    }
    
    /* Mark as inactive */
    entry->active = false;
    
    /* Compact if many inactive entries */
    if (loop->event_count > 32 && 
        loop->event_count > loop->event_capacity / 2) {
        ol_compact_events(loop);
    }
    
    ol_mutex_unlock(&loop->mutex);
    return OL_SUCCESS;
}

bool ol_event_loop_is_running(const ol_event_loop_t *loop) {
    return loop && loop->running;
}

size_t ol_event_loop_event_count(const ol_event_loop_t *loop) {
    if (!loop) {
        return 0;
    }
    
    size_t count = 0;
    
    ol_mutex_lock((ol_mutex_t*)&loop->mutex);
    for (size_t i = 0; i < loop->event_count; i++) {
        if (loop->events[i].active) {
            count++;
        }
    }
    ol_mutex_unlock((ol_mutex_t*)&loop->mutex);
    
    return count;
}