/**
 * @file ol_serialize.h
 * @brief Message serialization for inter-process communication in actor system
 * 
 * @details Provides message serialization capabilities for safe transfer of
 * messages between isolated actor processes. Supports multiple formats and
 * features like compression and encryption for performance and security.
 * 
 * Key features:
 * - Multiple serialization formats (binary, MessagePack, JSON, custom)
 * - Optional compression and encryption
 * - Data integrity validation with checksums
 * - Process ID tracking for message routing
 * - Custom serialization callbacks for complex data types
 * 
 * @note Serialization adds overhead but is necessary for safe inter-process
 *       communication. Consider performance implications for high-throughput
 *       scenarios.
 * 
 * @author OverLab Group
 * @version 1.3.0
 * @date 2026
 */

#ifndef OL_SERIALIZE_H
#define OL_SERIALIZE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Forward declaration from ol_actor_process.h */
/**
 * @brief Process ID type for tracking message senders and receivers
 */
typedef uint64_t ol_pid_t;

/**
 * @brief Serialization format enumeration
 * 
 * @details Different serialization formats offer trade-offs between
 * speed, size, and human-readability.
 */
typedef enum {
    OL_SERIALIZE_BINARY,      /**< Raw binary format (fastest, smallest) */
    OL_SERIALIZE_MSGPACK,     /**< MessagePack format (good balance) */
    OL_SERIALIZE_JSON,        /**< JSON format (human readable, largest) */
    OL_SERIALIZE_CUSTOM       /**< Custom format with user callbacks */
} ol_serialize_format_t;

/**
 * @brief Serialization flags for controlling behavior
 * 
 * @details Flags can be combined using bitwise OR to enable
 * multiple features simultaneously.
 */
typedef enum {
    OL_SERIALIZE_COMPRESS    = 1 << 0, /**< Enable data compression */
    OL_SERIALIZE_ENCRYPT     = 1 << 1, /**< Enable data encryption */
    OL_SERIALIZE_VALIDATE    = 1 << 2, /**< Add integrity checksum */
    OL_SERIALIZE_SHALLOW     = 1 << 3  /**< Shallow copy (no deep serialization) */
} ol_serialize_flags_t;

/**
 * @brief Serialized message structure
 * 
 * @details Contains both the serialized data and metadata needed for
 * deserialization, validation, and routing.
 */
typedef struct {
    uint8_t* data;            /**< Serialized data buffer (header + payload) */
    size_t size;              /**< Total buffer size in bytes */
    ol_serialize_format_t format; /**< Serialization format used */
    uint32_t flags;           /**< Serialization flags applied */
    uint64_t checksum;        /**< Data integrity checksum */
    uint64_t timestamp;       /**< Serialization timestamp (nanoseconds) */
    ol_pid_t sender_pid;      /**< Sender process ID (0 = unknown) */
    ol_pid_t receiver_pid;    /**< Receiver process ID (0 = broadcast) */
} ol_serialized_msg_t;

/**
 * @brief Custom serialization callbacks structure
 * 
 * @details Allows users to provide custom serialization logic for
 * complex data types or proprietary formats.
 */
typedef struct {
    /**
     * @brief Serialize custom data
     * 
     * @param data Original data to serialize
     * @param out_size Will be set to serialized data size
     * @return void* Serialized data buffer
     */
    void* (*serialize)(void* data, size_t* out_size);
    
    /**
     * @brief Deserialize custom data
     * 
     * @param serialized Serialized data buffer
     * @param size Size of serialized data
     * @return void* Deserialized data
     */
    void* (*deserialize)(void* serialized, size_t size);
    
    /**
     * @brief Free serialized data
     * 
     * @param serialized Serialized data to free
     */
    void (*free_serialized)(void* serialized);
} ol_serialize_callbacks_t;

/**
 * @brief Serialize data for inter-process transfer
 * 
 * @param data Data to serialize (must not be NULL)
 * @param size Data size in bytes (must be > 0)
 * @param format Serialization format to use
 * @param flags Serialization flags (bitwise OR of ol_serialize_flags_t)
 * @param sender_pid Sender process ID (0 for unknown)
 * @param receiver_pid Receiver process ID (0 for broadcast)
 * @return ol_serialized_msg_t* Serialized message, NULL on failure
 * 
 * @details The serialization process includes:
 * 1. Custom serialization (if callbacks set)
 * 2. Compression (if OL_SERIALIZE_COMPRESS flag)
 * 3. Encryption (if OL_SERIALIZE_ENCRYPT flag)
 * 4. Checksum calculation (if OL_SERIALIZE_VALIDATE flag)
 * 5. Header creation with metadata
 * 
 * @note The returned message must be freed with ol_serialize_free().
 */
ol_serialized_msg_t* ol_serialize(const void* data, size_t size,
                                 ol_serialize_format_t format,
                                 uint32_t flags,
                                 ol_pid_t sender_pid,
                                 ol_pid_t receiver_pid);

/**
 * @brief Deserialize message back to original data
 * 
 * @param msg Serialized message to deserialize (must not be NULL)
 * @param out_data Will receive pointer to deserialized data
 * @param out_size Will receive size of deserialized data
 * @return int OL_SUCCESS on success, OL_ERROR on failure
 * 
 * @details The deserialization process reverses the serialization:
 * 1. Header validation (magic, version)
 * 2. Decryption (if encrypted)
 * 3. Decompression (if compressed)
 * 4. Checksum validation (if validation flag)
 * 5. Custom deserialization (if custom format)
 * 
 * @note The caller is responsible for freeing the returned data with free().
 */
int ol_deserialize(const ol_serialized_msg_t* msg, void** out_data,
                  size_t* out_size);

/**
 * @brief Free serialized message and associated resources
 * 
 * @param msg Message to free (can be NULL)
 * 
 * @note Safe to call with NULL (no-op). Frees both the message
 * structure and its internal data buffer.
 */
void ol_serialize_free(ol_serialized_msg_t* msg);

/**
 * @brief Create deep copy of serialized message
 * 
 * @param src Source message to clone (must not be NULL)
 * @return ol_serialized_msg_t* Deep copy, NULL on failure
 * 
 * @note The returned copy is independent of the source. Both must
 * be freed separately with ol_serialize_free().
 */
ol_serialized_msg_t* ol_serialize_clone(const ol_serialized_msg_t* src);

/**
 * @brief Get message format
 * 
 * @param msg Serialized message
 * @return ol_serialize_format_t Format used, OL_SERIALIZE_BINARY if msg is NULL
 */
ol_serialize_format_t ol_serialize_get_format(const ol_serialized_msg_t* msg);

/**
 * @brief Get total message size
 * 
 * @param msg Serialized message
 * @return size_t Total size in bytes (including header), 0 if msg is NULL
 */
size_t ol_serialize_get_size(const ol_serialized_msg_t* msg);

/**
 * @brief Validate message integrity
 * 
 * @param msg Message to validate
 * @return bool true if message is valid, false otherwise
 * 
 * @details Performs basic validation including:
 * - NULL and size checks
 * - Magic number verification
 * - Version compatibility
 * - Checksum validation (if validation flag set)
 */
bool ol_serialize_validate(const ol_serialized_msg_t* msg);

/**
 * @brief Set custom serialization callbacks
 * 
 * @param callbacks Callback functions structure (NULL to clear)
 * 
 * @note Callbacks are used for OL_SERIALIZE_CUSTOM format.
 *       Setting callbacks is thread-safe but callbacks themselves
 *       must be thread-safe if used concurrently.
 */
void ol_serialize_set_callbacks(const ol_serialize_callbacks_t* callbacks);

/**
 * @brief Compress data using internal algorithm
 * 
 * @param data Data to compress
 * @param size Data size in bytes
 * @param out_size Will receive compressed size
 * @return void* Compressed data buffer, NULL on failure
 * 
 * @note This is a convenience function exposing the internal
 *       compression algorithm. Caller must free the returned buffer.
 */
void* ol_serialize_compress(const void* data, size_t size, size_t* out_size);

/**
 * @brief Decompress data compressed with internal algorithm
 * 
 * @param compressed Compressed data
 * @param size Compressed size in bytes
 * @param out_size Will receive decompressed size
 * @return void* Decompressed data buffer, NULL on failure
 * 
 * @note This function decompresses data compressed by
 *       ol_serialize_compress(). Caller must free the returned buffer.
 */
void* ol_serialize_decompress(const void* compressed, size_t size,
                             size_t* out_size);

#endif /* OL_SERIALIZE_H */
