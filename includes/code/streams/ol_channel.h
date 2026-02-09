/**
 * @file ol_channel.h
 * @brief Thread-safe channel implementation for OLSRT
 * @version 1.2.0
 * 
 * This header provides a FIFO channel implementation with optional bounded
 * capacity and item destructor support. Channels are thread-safe and support
 * blocking and non-blocking operations with deadlines.
 */

#ifndef OL_CHANNEL_H
#define OL_CHANNEL_H

#include "ol_common.h"
#include "ol_lock_mutex.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque channel handle */
typedef struct ol_channel ol_channel_t;

/**
 * @brief Item destructor function type for channels
 * @param item Item to destroy
 */
typedef void (*ol_chan_item_destructor)(void *item);

/**
 * @brief Create a new channel
 * 
 * @param capacity Maximum number of queued items (0 for unbounded)
 * @param dtor Destructor for queued items (optional)
 * @return New channel handle, or NULL on error
 */
OL_API ol_channel_t* ol_channel_create(size_t capacity,
                                       ol_chan_item_destructor dtor);

/**
 * @brief Destroy a channel and free all resources
 * 
 * All queued items are destroyed using the channel's destructor.
 * 
 * @param ch Channel to destroy (may be NULL)
 */
OL_API void ol_channel_destroy(ol_channel_t *ch);

/**
 * @brief Close a channel
 * 
 * Subsequent sends will fail, but receivers can drain remaining items.
 * 
 * @param ch Channel to close
 * @return OL_SUCCESS on success, OL_ERROR on invalid argument
 */
OL_API int ol_channel_close(ol_channel_t *ch);

/**
 * @brief Send an item with blocking wait
 * 
 * Blocks until space is available or channel is closed.
 * 
 * @param ch Channel handle
 * @param item Item to send (ownership transferred)
 * @return OL_SUCCESS on success, OL_CLOSED if channel closed,
 *         OL_ERROR on invalid argument
 */
OL_API int ol_channel_send(ol_channel_t *ch, void *item);

/**
 * @brief Send an item with deadline
 * 
 * @param ch Channel handle
 * @param item Item to send (ownership transferred)
 * @param deadline_ns Absolute deadline in nanoseconds (0 for infinite)
 * @return OL_SUCCESS on success, OL_CLOSED if channel closed,
 *         OL_TIMEOUT on timeout, OL_ERROR on error
 */
OL_API int ol_channel_send_deadline(ol_channel_t *ch,
                                    void *item,
                                    int64_t deadline_ns);

/**
 * @brief Try to send without blocking
 * 
 * @param ch Channel handle
 * @param item Item to send (ownership transferred)
 * @return 1 if sent, 0 if would block, OL_CLOSED if channel closed,
 *         OL_ERROR on error
 */
OL_API int ol_channel_try_send(ol_channel_t *ch, void *item);

/**
 * @brief Receive an item with blocking wait
 * 
 * @param ch Channel handle
 * @param out_item Output parameter for received item
 * @return 1 if item received, 0 if channel closed and empty,
 *         OL_ERROR on error
 */
OL_API int ol_channel_recv(ol_channel_t *ch, void **out_item);

/**
 * @brief Receive an item with deadline
 * 
 * @param ch Channel handle
 * @param out_item Output parameter for received item
 * @param deadline_ns Absolute deadline in nanoseconds (0 for infinite)
 * @return 1 if item received, 0 if channel closed and empty,
 *         OL_TIMEOUT on timeout, OL_ERROR on error
 */
OL_API int ol_channel_recv_deadline(ol_channel_t *ch,
                                    void **out_item,
                                    int64_t deadline_ns);

/**
 * @brief Try to receive without blocking
 * 
 * @param ch Channel handle
 * @param out_item Output parameter for received item
 * @return 1 if item received, 0 if would block or channel closed and empty,
 *         OL_ERROR on error
 */
OL_API int ol_channel_try_recv(ol_channel_t *ch, void **out_item);

/**
 * @brief Check if channel is closed
 * 
 * @param ch Channel handle
 * @return true if closed, false otherwise
 */
OL_API bool ol_channel_is_closed(const ol_channel_t *ch);

/**
 * @brief Get current queue length
 * 
 * @param ch Channel handle
 * @return Number of queued items
 */
OL_API size_t ol_channel_len(const ol_channel_t *ch);

/**
 * @brief Get channel capacity
 * 
 * @param ch Channel handle
 * @return Channel capacity (0 for unbounded)
 */
OL_API size_t ol_channel_capacity(const ol_channel_t *ch);

#ifdef __cplusplus
}
#endif

#endif /* OL_CHANNEL_H */