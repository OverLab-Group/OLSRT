/**
 * @file ol_serialize.h
 * @brief Message serialization for inter-process communication
 * 
 * @details Serializes messages for safe transfer between isolated processes.
 * Supports multiple formats and ensures data integrity.
 */

#ifndef OL_SERIALIZE_H
#define OL_SERIALIZE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Forward declaration from ol_actor_process.h */
typedef uint64_t ol_pid_t;

/** Serialization format */
typedef enum {
    OL_SERIALIZE_BINARY,      /**< Raw binary format (fastest) */
    OL_SERIALIZE_MSGPACK,     /**< MessagePack format */
    OL_SERIALIZE_JSON,        /**< JSON format (human readable) */
    OL_SERIALIZE_CUSTOM       /**< Custom format with callbacks */
} ol_serialize_format_t;

/** Serialization flags */
typedef enum {
    OL_SERIALIZE_COMPRESS    = 1 << 0, /**< Compress data */
    OL_SERIALIZE_ENCRYPT     = 1 << 1, /**< Encrypt data */
    OL_SERIALIZE_VALIDATE    = 1 << 2, /**< Add validation checksum */
    OL_SERIALIZE_SHALLOW     = 1 << 3  /**< Shallow copy (no deep serialization) */
} ol_serialize_flags_t;

/** Serialized message structure */
typedef struct {
    uint8_t* data;            /**< Serialized data */
    size_t size;              /**< Data size */
    ol_serialize_format_t format; /**< Serialization format */
    uint32_t flags;           /**< Serialization flags */
    uint64_t checksum;        /**< Data integrity checksum */
    uint64_t timestamp;       /**< Serialization timestamp */
    ol_pid_t sender_pid;      /**< Sender process ID */
    ol_pid_t receiver_pid;    /**< Receiver process ID */
} ol_serialized_msg_t;

/** Custom serialization callbacks */
typedef struct {
    void* (*serialize)(void* data, size_t* out_size);
    void* (*deserialize)(void* serialized, size_t size);
    void (*free_serialized)(void* serialized);
} ol_serialize_callbacks_t;

/**
 * @brief Serialize data for inter-process transfer
 * 
 * @param data Data to serialize
 * @param size Data size
 * @param format Serialization format
 * @param flags Serialization flags
 * @param sender_pid Sender process ID
 * @param receiver_pid Receiver process ID
 * @return ol_serialized_msg_t* Serialized message or NULL on failure
 */
ol_serialized_msg_t* ol_serialize(const void* data, size_t size,
                                 ol_serialize_format_t format,
                                 uint32_t flags,
                                 ol_pid_t sender_pid,
                                 ol_pid_t receiver_pid);

/**
 * @brief Deserialize message
 * 
 * @param msg Serialized message
 * @param out_data Output data pointer
 * @param out_size Output size pointer
 * @return int 0 on success, -1 on error
 */
int ol_deserialize(const ol_serialized_msg_t* msg, void** out_data,
                  size_t* out_size);

/**
 * @brief Free serialized message
 * 
 * @param msg Message to free
 */
void ol_serialize_free(ol_serialized_msg_t* msg);

/**
 * @brief Create deep copy of serialized message
 * 
 * @param src Source message
 * @return ol_serialized_msg_t* Deep copy
 */
ol_serialized_msg_t* ol_serialize_clone(const ol_serialized_msg_t* src);

/**
 * @brief Get message format
 * 
 * @param msg Serialized message
 * @return ol_serialize_format_t Format
 */
ol_serialize_format_t ol_serialize_get_format(const ol_serialized_msg_t* msg);

/**
 * @brief Get message size
 * 
 * @param msg Serialized message
 * @return size_t Size in bytes
 */
size_t ol_serialize_get_size(const ol_serialized_msg_t* msg);

/**
 * @brief Validate message integrity
 * 
 * @param msg Message to validate
 * @return bool True if message is valid
 */
bool ol_serialize_validate(const ol_serialized_msg_t* msg);

/**
 * @brief Set custom serialization callbacks
 * 
 * @param callbacks Callback functions
 */
void ol_serialize_set_callbacks(const ol_serialize_callbacks_t* callbacks);

/**
 * @brief Compress data
 * 
 * @param data Data to compress
 * @param size Data size
 * @param out_size Output compressed size
 * @return void* Compressed data
 */
void* ol_serialize_compress(const void* data, size_t size, size_t* out_size);

/**
 * @brief Decompress data
 * 
 * @param compressed Compressed data
 * @param size Compressed size
 * @param out_size Output decompressed size
 * @return void* Decompressed data
 */
void* ol_serialize_decompress(const void* compressed, size_t size,
                             size_t* out_size);

#endif /* OL_SERIALIZE_H */
