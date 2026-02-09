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
 * 
 * @author OverLab Group
 * @version 1.3.0
 * @date 2026
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

/**
 * @brief Opaque type representing an actor instance
 */
typedef struct ol_actor ol_actor_t;

/**
 * @brief Opaque type representing a supervisor
 */
typedef struct ol_supervisor ol_supervisor_t;

/**
 * @brief Opaque type representing an isolated process
 */
typedef struct ol_process ol_process_t;

/**
 * @brief Opaque type representing a memory arena
 */
typedef struct ol_arena ol_arena_t;

/* ==================== Type Definitions ==================== */

/**
 * @brief Actor behavior function type
 * 
 * @param actor Actor instance that received the message
 * @param message Pointer to the received message data
 * @return int Behavior result code:
 *            - 0: Continue processing messages normally
 *            - >0: Request graceful stop with the returned value as exit code
 *            - <0: Error occurred (treated as crash for supervision)
 * 
 * @note The behavior function should not block for long periods as it
 *       prevents the actor from processing other messages.
 */
typedef int (*ol_actor_behavior)(ol_actor_t* actor, void* message);

/**
 * @brief Message destructor function type
 * 
 * @param msg Pointer to message to be destroyed
 * 
 * @note This function is responsible for freeing any resources
 *       associated with the message. It is called automatically
 *       when a message is consumed or when the actor is destroyed.
 */
typedef void (*ol_actor_msg_destructor)(void* msg);

/**
 * @brief Value destructor function type for promise replies
 * 
 * @param value Pointer to value to be destroyed
 * 
 * @note Used to clean up reply values in ask/reply patterns.
 *       Similar to ol_actor_msg_destructor but specific to
 *       promise resolution values.
 */
typedef void (*ol_actor_value_destructor)(void* value);

/**
 * @brief Ask envelope structure for request/response pattern
 * 
 * This structure encapsulates a request message along with
 * the promise that will be resolved with the response.
 * It enables asynchronous request/response communication
 * between actors.
 */
typedef struct ol_ask_envelope {
    void* payload;                 /**< Message payload (the actual request data) */
    ol_promise_t* reply;           /**< Promise to resolve with reply value */
    ol_actor_t* sender;            /**< Sender actor (optional, can be NULL) */
    uint64_t ask_id;               /**< Unique ask identifier for tracking */
} ol_ask_envelope_t;

/**
 * @brief Actor performance statistics structure
 * 
 * Contains various metrics about actor performance and
 * mailbox usage for monitoring and debugging purposes.
 */
typedef struct ol_actor_stats {
    uint64_t processed_messages;   /**< Total number of processed messages */
    uint64_t processing_time_ns;   /**< Total processing time in nanoseconds */
    uint64_t avg_latency_ns;       /**< Average latency per message in nanoseconds */
    size_t mailbox_size;           /**< Current number of messages in mailbox */
    size_t mailbox_capacity;       /**< Maximum mailbox capacity (0 = unbounded) */
    size_t mailbox_peak;           /**< Peak mailbox size reached */
    size_t overflow_events;        /**< Number of mailbox overflow events */
} ol_actor_stats_t;

/* ==================== Actor Creation & Lifecycle ==================== */

/**
 * @brief Create a new actor instance
 * 
 * @param pool Parallel pool for execution (currently deprecated, kept for compatibility)
 * @param capacity Mailbox capacity (0 = unbounded)
 * @param dtor Message destructor function (can be NULL)
 * @param initial Initial behavior function (must not be NULL)
 * @param user_ctx User context pointer (can be NULL)
 * @return ol_actor_t* Pointer to newly created actor instance, NULL on failure
 * 
 * @note The actor is created in a stopped state. Call ol_actor_start() to begin
 *       message processing.
 * @warning The initial behavior function must not be NULL.
 */
ol_actor_t* ol_actor_create(ol_parallel_pool_t* pool,
                            size_t capacity,
                            ol_actor_msg_destructor dtor,
                            ol_actor_behavior initial,
                            void* user_ctx);

/**
 * @brief Start actor message processing
 * 
 * @param actor Actor instance to start
 * @return int Operation result:
 *         - 0: Success
 *         - -1: Error (e.g., actor is NULL or already running)
 * 
 * @note This function begins processing messages from the actor's mailbox.
 *       If the actor is already running, this function does nothing and returns 0.
 */
int ol_actor_start(ol_actor_t* actor);

/**
 * @brief Gracefully stop an actor
 * 
 * The actor will process all remaining messages in its mailbox before stopping.
 * 
 * @param actor Actor instance to stop
 * @return int Operation result:
 *         - 0: Success
 *         - -1: Error (e.g., actor is NULL)
 * 
 * @note This is a graceful shutdown. For immediate termination, use ol_actor_close().
 */
int ol_actor_stop(ol_actor_t* actor);

/**
 * @brief Immediately close actor mailbox and stop processing
 * 
 * The actor will stop as soon as possible, discarding any unprocessed messages.
 * 
 * @param actor Actor instance to close
 * @return int Operation result:
 *         - 0: Success
 *         - -1: Error (e.g., actor is NULL)
 * 
 * @warning This function may cause message loss. Use ol_actor_stop() for
 *          graceful shutdown when possible.
 */
int ol_actor_close(ol_actor_t* actor);

/**
 * @brief Destroy actor and free all associated resources
 * 
 * @param actor Actor instance to destroy (can be NULL)
 * 
 * @note If the actor is running, it will be stopped first.
 *       This function handles NULL input gracefully (no-op).
 */
void ol_actor_destroy(ol_actor_t* actor);

/* ==================== Message Passing ==================== */

/**
 * @brief Send message to actor (blocking)
 * 
 * @param actor Actor instance to receive the message
 * @param msg Message to send (ownership transferred to actor)
 * @return int Operation result:
 *         - 0: Success
 *         - -1: Error (e.g., actor is NULL, closed, or mailbox full)
 * 
 * @note This function blocks if the mailbox is full (for bounded mailboxes).
 *       For non-blocking send, use ol_actor_try_send().
 * @warning The message must be allocated with ol_arena_alloc() if the actor
 *          uses arena-based memory management.
 */
int ol_actor_send(ol_actor_t* actor, void* msg);

/**
 * @brief Send message to actor with timeout
 * 
 * @param actor Actor instance to receive the message
 * @param msg Message to send (ownership transferred to actor)
 * @param timeout_ms Timeout in milliseconds (0 = infinite, no timeout)
 * @return int Operation result:
 *         - 0: Success
 *         - -1: Error (e.g., actor is NULL, closed)
 *         - -3: Timeout expired before message could be sent
 * 
 * @note For bounded mailboxes, this function waits up to timeout_ms for space
 *       to become available.
 */
int ol_actor_send_timeout(ol_actor_t* actor, void* msg, uint32_t timeout_ms);

/**
 * @brief Try to send message without blocking
 * 
 * @param actor Actor instance to receive the message
 * @param msg Message to send (ownership transferred to actor)
 * @return int Operation result:
 *         - 1: Message sent successfully
 *         - 0: Mailbox full (would block)
 *         - -1: Error (e.g., actor is NULL or closed)
 * 
 * @note This function never blocks. It returns immediately with the result.
 */
int ol_actor_try_send(ol_actor_t* actor, void* msg);

/**
 * @brief Ask actor for response (request/response pattern)
 * 
 * @param actor Actor instance to send the request to
 * @param msg Request message (ownership transferred)
 * @return ol_future_t* Future object that will receive the response, NULL on error
 * 
 * @note This implements the ask pattern: send a request and get a future
 *       that will be fulfilled with the response. The actor must explicitly
 *       reply using ol_actor_reply_ok() or similar functions.
 * @warning The returned future must be destroyed with ol_future_destroy()
 *          when no longer needed.
 */
ol_future_t* ol_actor_ask(ol_actor_t* actor, void* msg);

/* ==================== Behavior Control ==================== */

/**
 * @brief Change actor behavior function
 * 
 * @param actor Actor instance to modify
 * @param behavior New behavior function (must not be NULL)
 * @return int Operation result:
 *         - 0: Success
 *         - -1: Error (e.g., actor or behavior is NULL)
 * 
 * @note This function can be called from within the actor's behavior
 *       to implement state machines. The new behavior will be used
 *       for subsequent messages.
 */
int ol_actor_become(ol_actor_t* actor, ol_actor_behavior behavior);

/**
 * @brief Get actor user context pointer
 * 
 * @param actor Actor instance
 * @return void* User context pointer (as set during creation or via ol_actor_set_context)
 * 
 * @note Returns NULL if actor is NULL.
 */
void* ol_actor_get_context(const ol_actor_t* actor);

/**
 * @brief Set actor user context pointer
 * 
 * @param actor Actor instance to modify
 * @param context New user context pointer (can be NULL)
 * 
 * @note This is typically used to pass application-specific data
 *       to the actor's behavior function.
 */
void ol_actor_set_context(ol_actor_t* actor, void* context);

/* ==================== Ask/Reply Helpers ==================== */

/**
 * @brief Reply to ask envelope with success value
 * 
 * @param envelope Ask envelope to reply to
 * @param value Reply value (ownership transferred)
 * @param dtor Value destructor function (can be NULL)
 * 
 * @note This function fulfills the promise in the ask envelope with the
 *       provided value. The envelope is automatically freed after this call.
 * @warning This function must only be called from within the actor's
 *          behavior function that received the ask envelope.
 */
void ol_actor_reply_ok(ol_ask_envelope_t* envelope, void* value, ol_actor_value_destructor dtor);

/**
 * @brief Reply to ask envelope with error
 * 
 * @param envelope Ask envelope to reply to
 * @param error_code Error code to report
 * 
 * @note This function rejects the promise in the ask envelope with the
 *       provided error code. The envelope is automatically freed after this call.
 */
void ol_actor_reply_error(ol_ask_envelope_t* envelope, int error_code);

/**
 * @brief Cancel ask envelope (no reply will be sent)
 * 
 * @param envelope Ask envelope to cancel
 * 
 * @note This function cancels the promise in the ask envelope, indicating
 *       that no reply will be sent. Useful for cleanup when the ask cannot
 *       be processed.
 */
void ol_actor_reply_cancel(ol_ask_envelope_t* envelope);

/* ==================== Introspection ==================== */

/**
 * @brief Check if actor is currently running
 * 
 * @param actor Actor instance to check
 * @return bool true if actor is running, false otherwise (or if actor is NULL)
 * 
 * @note An actor is considered running if it has been started and has not
 *       yet been stopped or crashed.
 */
bool ol_actor_is_running(const ol_actor_t* actor);

/**
 * @brief Get current mailbox length (number of pending messages)
 * 
 * @param actor Actor instance
 * @return size_t Number of messages currently in the mailbox, 0 if actor is NULL
 * 
 * @note This includes both messages in the ring buffer and overflow list.
 */
size_t ol_actor_mailbox_length(const ol_actor_t* actor);

/**
 * @brief Get mailbox capacity
 * 
 * @param actor Actor instance
 * @return size_t Mailbox capacity (0 = unbounded), 0 if actor is NULL
 */
size_t ol_actor_mailbox_capacity(const ol_actor_t* actor);

/**
 * @brief Get current actor (if called from within actor behavior)
 * 
 * @return ol_actor_t* Current actor instance, NULL if not called from actor context
 * 
 * @note This function uses thread-local storage to track the currently
 *       executing actor. It only works within actor behavior functions.
 */
ol_actor_t* ol_actor_self(void);

/* ==================== Extended API ==================== */

/**
 * @brief Get actor's isolated process
 * 
 * @param actor Actor instance
 * @return ol_process_t* Actor's isolated process, NULL if actor is NULL
 * 
 * @note Each actor runs in its own isolated process for memory safety.
 */
ol_process_t* ol_actor_get_process(const ol_actor_t* actor);

/**
 * @brief Get actor's private memory arena
 * 
 * @param actor Actor instance
 * @return ol_arena_t* Actor's private memory arena, NULL if actor is NULL
 * 
 * @note Each actor has its own memory arena for allocation isolation.
 */
ol_arena_t* ol_actor_get_arena(const ol_actor_t* actor);

/**
 * @brief Link two actors (monitor each other)
 * 
 * @param actor1 First actor to link
 * @param actor2 Second actor to link
 * @return int Operation result:
 *         - 0: Success
 *         - OL_ERROR: Error (e.g., either actor is NULL)
 * 
 * @note Linked actors monitor each other's lifetime. If one actor crashes,
 *       the other will be notified.
 */
int ol_actor_link(ol_actor_t* actor1, ol_actor_t* actor2);

/**
 * @brief Monitor another actor (one-way monitoring)
 * 
 * @param monitor Actor that will monitor the target
 * @param target Actor to be monitored
 * @return uint64_t Monitor reference ID (0 on error)
 * 
 * @note The monitor will be notified if the target crashes or exits.
 *       Use the returned reference to later remove the monitor.
 */
uint64_t ol_actor_monitor(ol_actor_t* monitor, ol_actor_t* target);

/**
 * @brief Get actor performance statistics
 * 
 * @param actor Actor instance
 * @param stats Pointer to stats structure to fill
 * @return int Operation result:
 *         - OL_SUCCESS: Success
 *         - OL_ERROR: Error (e.g., actor or stats is NULL)
 * 
 * @note The stats structure is filled with current performance metrics.
 */
int ol_actor_get_stats(const ol_actor_t* actor, ol_actor_stats_t* stats);

/**
 * @brief Process messages in batch mode for better performance
 * 
 * @param actor Actor instance
 * @param max_batch_size Maximum number of messages to process in batch
 * @return size_t Number of messages actually processed
 * 
 * @note This function can be called externally to process multiple messages
 *       at once, reducing context switch overhead. It's useful for optimizing
 *       throughput in performance-critical scenarios.
 */
size_t ol_actor_process_batch(ol_actor_t* actor, size_t max_batch_size);

#ifdef __cplusplus
}
#endif

#endif /* OL_ACTOR_H */
