/**
 * @file ol_promise.h
 * @brief Promise/Future abstraction for asynchronous programming
 * @version 1.2.0
 * 
 * This header provides a promise/future implementation for handling
 * asynchronous results with thread-safe resolution and continuation support.
 */

#ifndef OL_PROMISE_H
#define OL_PROMISE_H

#include "ol_common.h"
#include "ol_event_loop.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque promise handle */
typedef struct ol_promise ol_promise_t;

/** @brief Opaque future handle */
typedef struct ol_future ol_future_t;

/** @brief Promise state enumeration */
typedef enum {
    OL_PROMISE_PENDING,   /**< Not yet resolved */
    OL_PROMISE_FULFILLED, /**< Resolved with value */
    OL_PROMISE_REJECTED,  /**< Rejected with error */
    OL_PROMISE_CANCELED   /**< Canceled */
} ol_promise_state_t;

/** @brief Value destructor function type */
typedef void (*ol_value_destructor)(void *ptr);

/** @brief Future callback function type */
typedef void (*ol_future_cb)(struct ol_event_loop *loop,
                             ol_promise_state_t state,
                             const void *value,
                             int error_code,
                             void *user_data);

/**
 * @brief Create a new promise
 * 
 * @param loop Event loop for waking on resolution (optional)
 * @return New promise handle, or NULL on error
 */
OL_API ol_promise_t* ol_promise_create(struct ol_event_loop *loop);

/**
 * @brief Destroy a promise
 * 
 * @param p Promise to destroy (may be NULL)
 */
OL_API void ol_promise_destroy(ol_promise_t *p);

/**
 * @brief Get future associated with promise
 * 
 * @param p Promise
 * @return Future handle, or NULL on error
 * @note The future must be destroyed with ol_future_destroy()
 */
OL_API ol_future_t* ol_promise_get_future(ol_promise_t *p);

/**
 * @brief Fulfill promise with value
 * 
 * @param p Promise
 * @param value Result value (ownership transferred)
 * @param dtor Destructor for value (optional)
 * @return OL_SUCCESS on success, OL_ERROR on error (e.g., already resolved)
 */
OL_API int ol_promise_fulfill(ol_promise_t *p,
                              void *value,
                              ol_value_destructor dtor);

/**
 * @brief Reject promise with error code
 * 
 * @param p Promise
 * @param error_code Error code
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_promise_reject(ol_promise_t *p, int error_code);

/**
 * @brief Cancel promise
 * 
 * @param p Promise
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_promise_cancel(ol_promise_t *p);

/**
 * @brief Get promise state
 * 
 * @param p Promise
 * @return Current promise state
 */
OL_API ol_promise_state_t ol_promise_state(const ol_promise_t *p);

/**
 * @brief Check if promise is done (not pending)
 * 
 * @param p Promise
 * @return true if not pending, false otherwise
 */
OL_API bool ol_promise_is_done(const ol_promise_t *p);

/**
 * @brief Destroy a future
 * 
 * @param f Future to destroy (may be NULL)
 */
OL_API void ol_future_destroy(ol_future_t *f);

/**
 * @brief Await future resolution
 * 
 * @param f Future
 * @param deadline_ns Absolute deadline in nanoseconds (0 for infinite)
 * @return 1 on completion, OL_TIMEOUT on timeout, OL_ERROR on error
 */
OL_API int ol_future_await(ol_future_t *f, int64_t deadline_ns);

/**
 * @brief Add continuation to future
 * 
 * @param f Future
 * @param cb Callback to invoke when future resolves
 * @param user_data User data passed to callback
 * @return OL_SUCCESS on success, OL_ERROR on error
 */
OL_API int ol_future_then(ol_future_t *f,
                          ol_future_cb cb,
                          void *user_data);

/**
 * @brief Get value from fulfilled future (const)
 * 
 * @param f Future
 * @return Const pointer to value, or NULL if not fulfilled
 */
OL_API const void* ol_future_get_value_const(const ol_future_t *f);

/**
 * @brief Take value from fulfilled future (transfer ownership)
 * 
 * @param f Future
 * @return Pointer to value (caller owns), or NULL if not fulfilled
 */
OL_API void* ol_future_take_value(ol_future_t *f);

/**
 * @brief Get error code from rejected future
 * 
 * @param f Future
 * @return Error code, or 0 if not rejected
 */
OL_API int ol_future_error(const ol_future_t *f);

/**
 * @brief Get future state
 * 
 * @param f Future
 * @return Current future state
 */
OL_API ol_promise_state_t ol_future_state(const ol_future_t *f);

#ifdef __cplusplus
}
#endif

#endif /* OL_PROMISE_H */