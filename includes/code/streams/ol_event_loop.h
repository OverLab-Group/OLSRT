/**
 * @file ol_event_loop.h
 * @brief Portable event loop with I/O and timer support
 * @version 1.2.0
 * 
 * This header provides a single-threaded event loop implementation with
 * support for I/O events, timers, and wake mechanisms.
 */

#ifndef OL_EVENT_LOOP_H
#define OL_EVENT_LOOP_H

#include "ol_common.h"
#include "ol_poller.h"
#include "ol_deadlines.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque event loop handle */
typedef struct ol_event_loop ol_event_loop_t;

/** @brief Event type enumeration */
typedef enum {
    OL_EV_IO,    /**< I/O event */
    OL_EV_TIMER  /**< Timer event */
} ol_ev_type_t;

/** @brief Event callback type */
typedef void (*ol_event_cb)(ol_event_loop_t *loop,
                           ol_ev_type_t type,
                           int fd,
                           void *user_data);

/**
 * @brief Create a new event loop
 * 
 * @return New event loop handle, or NULL on error
 */
OL_API ol_event_loop_t* ol_event_loop_create(void);

/**
 * @brief Destroy an event loop
 * 
 * @param loop Event loop to destroy (may be NULL)
 */
OL_API void ol_event_loop_destroy(ol_event_loop_t *loop);

/**
 * @brief Run the event loop
 * 
 * Runs the event loop until ol_event_loop_stop() is called.
 * 
 * @param loop Event loop to run
 * @return OL_SUCCESS on normal exit, OL_ERROR on error
 */
OL_API int ol_event_loop_run(ol_event_loop_t *loop);

/**
 * @brief Stop the event loop
 * 
 * Signals the event loop to stop on the next iteration.
 * 
 * @param loop Event loop to stop
 */
OL_API void ol_event_loop_stop(ol_event_loop_t *loop);

/**
 * @brief Wake the event loop
 * 
 * Can be called from another thread to wake the event loop.
 * 
 * @param loop Event loop to wake
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_event_loop_wake(ol_event_loop_t *loop);

/**
 * @brief Register an I/O event
 * 
 * @param loop Event loop
 * @param fd File descriptor to monitor
 * @param mask Poll mask (OL_POLL_IN/OL_POLL_OUT)
 * @param cb Callback function
 * @param user_data User data passed to callback
 * @return Event ID (>0) on success, 0 on error
 */
OL_API uint64_t ol_event_loop_register_io(ol_event_loop_t *loop,
                                          int fd,
                                          uint32_t mask,
                                          ol_event_cb cb,
                                          void *user_data);

/**
 * @brief Modify I/O event mask
 * 
 * @param loop Event loop
 * @param id Event ID to modify
 * @param mask New poll mask
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_event_loop_mod_io(ol_event_loop_t *loop,
                                uint64_t id,
                                uint32_t mask);

/**
 * @brief Register a timer event
 * 
 * @param loop Event loop
 * @param deadline Timer deadline
 * @param periodic_ns Periodic interval in ns (0 for one-shot)
 * @param cb Callback function
 * @param user_data User data passed to callback
 * @return Event ID (>0) on success, 0 on error
 */
OL_API uint64_t ol_event_loop_register_timer(ol_event_loop_t *loop,
                                             ol_deadline_t deadline,
                                             int64_t periodic_ns,
                                             ol_event_cb cb,
                                             void *user_data);

/**
 * @brief Unregister an event
 * 
 * @param loop Event loop
 * @param id Event ID to unregister
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_event_loop_unregister(ol_event_loop_t *loop, uint64_t id);

/**
 * @brief Check if event loop is running
 * 
 * @param loop Event loop
 * @return true if running, false otherwise
 */
OL_API bool ol_event_loop_is_running(const ol_event_loop_t *loop);

/**
 * @brief Get number of registered events
 * 
 * @param loop Event loop
 * @return Number of active events
 */
OL_API size_t ol_event_loop_event_count(const ol_event_loop_t *loop);

#ifdef __cplusplus
}
#endif

#endif /* OL_EVENT_LOOP_H */