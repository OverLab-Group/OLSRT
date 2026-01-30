/**
 * @file ol_actor.h
 * @brief Actor Model Implementation for OLSRT
 * 
 * @details
 * This module implements a lightweight actor system with message passing,
 * behaviors, ask/reply patterns, and supervision integration.
 * Actors are concurrent entities that process messages sequentially.
 * 
 * @note
 * - Thread-safe: all public APIs are thread-safe
 * - Memory-safe: clear ownership semantics
 * - Cross-platform: works on Windows, Linux, macOS, BSD
 */

#ifndef OL_ACTOR_H
#define OL_ACTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ol_channel.h"
#include "ol_parallel.h"
#include "ol_promise.h"
#include "ol_lock_mutex.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Forward Declarations ==================== */
typedef struct ol_actor ol_actor_t;
typedef struct ol_supervisor ol_supervisor_t;

/* ==================== Type Definitions ==================== */

/**
 * @brief Actor behavior function type
 * 
 * @param actor Actor instance
 * @param message Received message
 * @return int Behavior result:
 *            0: continue processing
 *           >0: request graceful stop
 *           <0: error (treated as crash for supervision)
 */
typedef int (*ol_actor_behavior)(ol_actor_t* actor, void* message);

/**
 * @brief Message destructor function type
 * 
 * @param msg Message to destroy
 */
typedef void (*ol_actor_msg_destructor)(void* msg);

/**
 * @brief Value destructor for promise replies
 */
typedef void (*ol_actor_value_destructor)(void* value);

/**
 * @brief Ask envelope for request/response pattern
 */
typedef struct ol_ask_envelope {
    void* payload;                 /**< Message payload */
    ol_promise_t* reply;           /**< Promise to resolve with reply */
    ol_actor_t* sender;            /**< Sender actor (optional) */
    uint64_t ask_id;               /**< Unique ask identifier */
} ol_ask_envelope_t;

/* ==================== Actor Creation & Lifecycle ==================== */

/**
 * @brief Create a new actor
 * 
 * @param pool Parallel pool for execution
 * @param capacity Mailbox capacity (0 = unbounded)
 * @param dtor Message destructor (can be NULL)
 * @param initial Initial behavior function
 * @param user_ctx User context pointer
 * @return ol_actor_t* New actor instance, NULL on failure
 */
ol_actor_t* ol_actor_create(ol_parallel_pool_t* pool,
                            size_t capacity,
                            ol_actor_msg_destructor dtor,
                            ol_actor_behavior initial,
                            void* user_ctx);

/**
 * @brief Start actor message processing
 * 
 * @param actor Actor instance
 * @return int 0 on success, -1 on error
 */
int ol_actor_start(ol_actor_t* actor);

/**
 * @brief Gracefully stop an actor
 * 
 * Actor will process remaining messages before stopping
 * 
 * @param actor Actor instance
 * @return int 0 on success, -1 on error
 */
int ol_actor_stop(ol_actor_t* actor);

/**
 * @brief Immediately close actor mailbox
 * 
 * Actor will stop as soon as possible
 * 
 * @param actor Actor instance
 * @return int 0 on success, -1 on error
 */
int ol_actor_close(ol_actor_t* actor);

/**
 * @brief Destroy actor and free resources
 * 
 * @param actor Actor instance (can be NULL)
 */
void ol_actor_destroy(ol_actor_t* actor);

/* ==================== Message Passing ==================== */

/**
 * @brief Send message to actor (blocking)
 * 
 * @param actor Actor instance
 * @param msg Message to send
 * @return int 0 on success, -1 on error
 */
int ol_actor_send(ol_actor_t* actor, void* msg);

/**
 * @brief Send message with timeout
 * 
 * @param actor Actor instance
 * @param msg Message to send
 * @param timeout_ms Timeout in milliseconds (0 = infinite)
 * @return int 0 on success, -1 on error, -3 on timeout
 */
int ol_actor_send_timeout(ol_actor_t* actor, void* msg, uint32_t timeout_ms);

/**
 * @brief Try to send message without blocking
 * 
 * @param actor Actor instance
 * @param msg Message to send
 * @return int 1 if sent, 0 if mailbox full, -1 on error
 */
int ol_actor_try_send(ol_actor_t* actor, void* msg);

/**
 * @brief Ask actor for response (request/response pattern)
 * 
 * @param actor Actor instance
 * @param msg Request message
 * @return ol_future_t* Future for response, NULL on error
 */
ol_future_t* ol_actor_ask(ol_actor_t* actor, void* msg);

/* ==================== Behavior Control ==================== */

/**
 * @brief Change actor behavior
 * 
 * @param actor Actor instance
 * @param behavior New behavior function
 * @return int 0 on success, -1 on error
 */
int ol_actor_become(ol_actor_t* actor, ol_actor_behavior behavior);

/**
 * @brief Get actor user context
 * 
 * @param actor Actor instance
 * @return void* User context pointer
 */
void* ol_actor_get_context(const ol_actor_t* actor);

/**
 * @brief Set actor user context
 * 
 * @param actor Actor instance
 * @param context New context pointer
 */
void ol_actor_set_context(ol_actor_t* actor, void* context);

/* ==================== Ask/Reply Helpers ==================== */

/**
 * @brief Reply to ask envelope with success
 * 
 * @param envelope Ask envelope
 * @param value Reply value
 * @param dtor Value destructor (can be NULL)
 */
void ol_actor_reply_ok(ol_ask_envelope_t* envelope, void* value, ol_actor_value_destructor dtor);

/**
 * @brief Reply to ask envelope with error
 * 
 * @param envelope Ask envelope
 * @param error_code Error code
 */
void ol_actor_reply_error(ol_ask_envelope_t* envelope, int error_code);

/**
 * @brief Cancel ask envelope
 * 
 * @param envelope Ask envelope
 */
void ol_actor_reply_cancel(ol_ask_envelope_t* envelope);

/* ==================== Introspection ==================== */

/**
 * @brief Check if actor is running
 * 
 * @param actor Actor instance
 * @return bool true if running
 */
bool ol_actor_is_running(const ol_actor_t* actor);

/**
 * @brief Get mailbox length
 * 
 * @param actor Actor instance
 * @return size_t Number of pending messages
 */
size_t ol_actor_mailbox_length(const ol_actor_t* actor);

/**
 * @brief Get mailbox capacity
 * 
 * @param actor Actor instance
 * @return size_t Mailbox capacity (0 = unbounded)
 */
size_t ol_actor_mailbox_capacity(const ol_actor_t* actor);

/**
 * @brief Get current actor (if called from within actor behavior)
 * 
 * @return ol_actor_t* Current actor, NULL if not in actor context
 */
ol_actor_t* ol_actor_self(void);

#ifdef __cplusplus
}
#endif

#endif /* OL_ACTOR_H */
