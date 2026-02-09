/**
 * @file ol_coroutines.h
 * @brief User-level coroutine abstraction for OLSRT
 * @version 1.2.0
 * 
 * This header provides a coroutine abstraction built on platform-specific
 * green thread backends. Coroutines are cooperative and run on a single-
 * threaded scheduler.
 */

#ifndef OL_COROUTINES_H
#define OL_COROUTINES_H

#include "ol_common.h"
#include "ol_green_threads.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque coroutine handle */
typedef struct ol_co ol_co_t;

/**
 * @brief Coroutine entry function type
 * @param arg User-provided argument
 * @return Final result (ownership transferred to joiner)
 */
typedef void* (*ol_co_entry_fn)(void *arg);

/**
 * @brief Initialize the coroutine scheduler
 * 
 * Must be called once per thread before spawning coroutines.
 * 
 * @return OL_SUCCESS on success, OL_ERROR on failure
 */
OL_API int ol_coroutine_scheduler_init(void);

/**
 * @brief Shutdown the coroutine scheduler
 */
OL_API void ol_coroutine_scheduler_shutdown(void);

/**
 * @brief Spawn a new coroutine
 * 
 * @param entry Coroutine entry function (must not be NULL)
 * @param arg Argument passed to entry function
 * @param stack_size Advisory stack size in bytes
 * @return New coroutine handle, or NULL on error
 */
OL_API ol_co_t* ol_co_spawn(ol_co_entry_fn entry,
                            void *arg,
                            size_t stack_size);

/**
 * @brief Resume a coroutine
 * 
 * @param co Coroutine to resume (must not be NULL)
 * @param payload Payload passed to coroutine (optional)
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_co_resume(ol_co_t *co, void *payload);

/**
 * @brief Yield from current coroutine
 * 
 * Must only be called from within a coroutine context.
 * 
 * @param payload Payload to yield to caller (optional)
 * @return Payload from resuming caller
 */
OL_API void* ol_co_yield(void *payload);

/**
 * @brief Join a coroutine
 * 
 * Blocks until the coroutine completes and returns its result.
 * 
 * @param co Coroutine to join (must not be NULL)
 * @return Coroutine result, or NULL on error
 */
OL_API void* ol_co_join(ol_co_t *co);

/**
 * @brief Cancel a coroutine
 * 
 * Requests cooperative cancellation. The coroutine should check
 * ol_co_is_canceled() periodically.
 * 
 * @param co Coroutine to cancel (must not be NULL)
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_co_cancel(ol_co_t *co);

/**
 * @brief Destroy a coroutine handle
 * 
 * If the coroutine is still running, attempts cooperative cancellation
 * and join before destroying.
 * 
 * @param co Coroutine to destroy (may be NULL)
 */
OL_API void ol_co_destroy(ol_co_t *co);

/**
 * @brief Check if coroutine is alive
 * 
 * @param co Coroutine handle
 * @return true if not done and not canceled, false otherwise
 */
OL_API bool ol_co_is_alive(const ol_co_t *co);

/**
 * @brief Check if coroutine has been requested to cancel
 * 
 * @param co Coroutine handle
 * @return true if canceled, false otherwise
 */
OL_API bool ol_co_is_canceled(const ol_co_t *co);

#ifdef __cplusplus
}
#endif

#endif /* OL_COROUTINES_H */