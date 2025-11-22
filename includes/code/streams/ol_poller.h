#ifndef OL_POLLER_H
#define OL_POLLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ol_deadlines.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Unified event mask for readability across backends. */
enum {
    OL_POLL_IN   = 0x01, // readable
    OL_POLL_OUT  = 0x02, // writable
    OL_POLL_ERR  = 0x04, // error/hup
};

typedef struct {
    int   fd;       // file descriptor
    uint32_t mask;  // OL_POLL_* mask
    uint64_t tag;   // user tag (e.g., event id for dispatch layer)
} ol_poll_event_t;

/* Opaque poller type per backend (epoll/kqueue/select). */
typedef struct ol_poller ol_poller_t;

/* Lifecycle */
ol_poller_t* ol_poller_create(void);
void         ol_poller_destroy(ol_poller_t *p);

/* Registration */
int ol_poller_add(ol_poller_t *p, int fd, uint32_t mask, uint64_t tag);
int ol_poller_mod(ol_poller_t *p, int fd, uint32_t mask, uint64_t tag);
int ol_poller_del(ol_poller_t *p, int fd);

/* Wait for events until deadline or infinite if dl.when_ns == 0. 
 * Returns number of events, 0 on timeout, negative on error.
 * Fills up to 'cap' events in 'out'.
 */
int ol_poller_wait(ol_poller_t *p, ol_deadline_t dl, ol_poll_event_t *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* OL_POLLER_H */
