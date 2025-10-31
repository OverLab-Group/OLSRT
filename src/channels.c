/* OLSRT - Channels (MPMC) */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>

/* Internal queue */
typedef struct ol_chan_core_s {
    void           **buf;
    size_t           cap;
    size_t           head;
    size_t           tail;
    size_t           count;
    int              closed;
    pthread_mutex_t  mu;
    pthread_cond_t   cv_not_full;   /* signal for senders */
    pthread_cond_t   cv_not_empty;  /* signal for receivers */
} ol_chan_core_t;

struct ol_channel_s {
    ol_chan_core_t c;
};

/* Timed wait helper: waits on condition with optional timeout_ms */
static int ol_cond_wait_ms(pthread_cond_t *cv, pthread_mutex_t *mu, uint64_t timeout_ms) {
    if (timeout_ms == 0) {
        return pthread_cond_wait(cv, mu);
    }
#if defined(_WIN32)
    /* Windows compat layer provides timed wait via SleepConditionVariableCS */
    DWORD ms = (DWORD)timeout_ms;
    return SleepConditionVariableCS(cv, mu, ms) ? 0 : -1;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += (time_t)(timeout_ms / 1000);
    ts.tv_nsec += (long)((timeout_ms % 1000) * 1000000L);
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return pthread_cond_timedwait(cv, mu, &ts);
#endif
}

/* Create channel with capacity */
ol_channel_t* ol_channel_create(const ol_channel_opts_t *opts) {
    size_t cap = (opts && opts->capacity) ? opts->capacity : 1024;
    ol_channel_t *ch = (ol_channel_t*)calloc(1, sizeof(*ch));
    if (!ch) return NULL;

    ch->c.buf = (void**)calloc(cap, sizeof(void*));
    if (!ch->c.buf) { free(ch); return NULL; }

    ch->c.cap   = cap;
    ch->c.head  = 0;
    ch->c.tail  = 0;
    ch->c.count = 0;
    ch->c.closed= 0;

    pthread_mutex_init(&ch->c.mu, NULL);
    pthread_cond_init(&ch->c.cv_not_full, NULL);
    pthread_cond_init(&ch->c.cv_not_empty, NULL);

    return ch;
}

/* Send message (blocks until space or timeout); returns OL_OK/OL_ERR_TIMEOUT/OL_ERR_CLOSED */
int ol_channel_send(ol_channel_t *ch, void *msg, uint64_t timeout_ms) {
    if (!ch) return OL_ERR_STATE;

    pthread_mutex_lock(&ch->c.mu);

    while (!ch->c.closed && ch->c.count == ch->c.cap) {
        if (ol_cond_wait_ms(&ch->c.cv_not_full, &ch->c.mu, timeout_ms) != 0) {
            pthread_mutex_unlock(&ch->c.mu);
            return OL_ERR_TIMEOUT;
        }
    }

    if (ch->c.closed) {
        pthread_mutex_unlock(&ch->c.mu);
        return OL_ERR_CLOSED;
    }

    ch->c.buf[ch->c.tail] = msg;
    ch->c.tail = (ch->c.tail + 1) % ch->c.cap;
    ch->c.count++;

    pthread_cond_signal(&ch->c.cv_not_empty);
    pthread_mutex_unlock(&ch->c.mu);
    return OL_OK;
}

/* Receive message (blocks until item or timeout); returns NULL on timeout/closed-empty */
void* ol_channel_recv(ol_channel_t *ch, uint64_t timeout_ms) {
    if (!ch) return NULL;

    pthread_mutex_lock(&ch->c.mu);

    while (!ch->c.closed && ch->c.count == 0) {
        if (ol_cond_wait_ms(&ch->c.cv_not_empty, &ch->c.mu, timeout_ms) != 0) {
            pthread_mutex_unlock(&ch->c.mu);
            return NULL;
        }
    }

    if (ch->c.count == 0 && ch->c.closed) {
        pthread_mutex_unlock(&ch->c.mu);
        return NULL;
    }

    void *msg = ch->c.buf[ch->c.head];
    ch->c.head = (ch->c.head + 1) % ch->c.cap;
    ch->c.count--;

    pthread_cond_signal(&ch->c.cv_not_full);
    pthread_mutex_unlock(&ch->c.mu);
    return msg;
}

/* Close channel: wakes all waiters; subsequent sends fail, recvs drain then return NULL */
int ol_channel_close(ol_channel_t *ch) {
    if (!ch) return OL_ERR_STATE;
    pthread_mutex_lock(&ch->c.mu);
    ch->c.closed = 1;
    pthread_cond_broadcast(&ch->c.cv_not_full);
    pthread_cond_broadcast(&ch->c.cv_not_empty);
    pthread_mutex_unlock(&ch->c.mu);
    return OL_OK;
}
