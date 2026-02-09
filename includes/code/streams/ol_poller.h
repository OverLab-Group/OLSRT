/**
 * @file ol_poller.h
 * @brief Cross-platform I/O polling abstraction
 * @version 1.2.0
 * 
 * This header provides a uniform API for I/O polling across different
 * platforms (epoll, kqueue, select).
 */

#ifndef OL_POLLER_H
#define OL_POLLER_H

#include "ol_common.h"
#include "ol_deadlines.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque poller handle */
typedef struct ol_poller ol_poller_t;

/** @brief Poll event structure */
typedef struct {
    int fd;           /**< File descriptor */
    uint32_t mask;    /**< Event mask (OL_POLL_IN/OUT/ERR) */
    uint64_t tag;     /**< User tag associated with fd */
} ol_poll_event_t;

/** @brief Poll event masks */
#define OL_POLL_IN  0x01  /**< Readable */
#define OL_POLL_OUT 0x02  /**< Writable */
#define OL_POLL_ERR 0x04  /**< Error condition */

/**
 * @brief Create a new poller instance
 * 
 * Automatically selects the best backend for the platform:
 * - Linux: epoll
 * - macOS/BSD: kqueue
 * - Fallback: select
 * 
 * @return New poller handle, or NULL on error
 */
OL_API ol_poller_t* ol_poller_create(void);

/**
 * @brief Destroy a poller instance
 * 
 * @param p Poller to destroy
 */
OL_API void ol_poller_destroy(ol_poller_t *p);

/**
 * @brief Add a file descriptor to poller
 * 
 * @param p Poller instance
 * @param fd File descriptor
 * @param mask Event mask (OL_POLL_IN/OUT)
 * @param tag User tag associated with fd
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_poller_add(ol_poller_t *p,
                         int fd,
                         uint32_t mask,
                         uint64_t tag);

/**
 * @brief Modify poller event mask for a file descriptor
 * 
 * @param p Poller instance
 * @param fd File descriptor
 * @param mask New event mask
 * @param tag New user tag
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_poller_mod(ol_poller_t *p,
                         int fd,
                         uint32_t mask,
                         uint64_t tag);

/**
 * @brief Remove a file descriptor from poller
 * 
 * @param p Poller instance
 * @param fd File descriptor to remove
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_poller_del(ol_poller_t *p, int fd);

/**
 * @brief Wait for events
 * 
 * @param p Poller instance
 * @param dl Deadline to wait until (0 for infinite)
 * @param out Output array for events
 * @param cap Capacity of output array
 * @return Number of events returned (0 on timeout, negative on error)
 */
OL_API int ol_poller_wait(ol_poller_t *p,
                          ol_deadline_t dl,
                          ol_poll_event_t *out,
                          int cap);

#ifdef __cplusplus
}
#endif

#endif /* OL_POLLER_H */