/**
 * @file ol_actor_serialization.h
 * @brief Zero-copy message serialization with architecture-specific optimizations
 * @version 1.3.0
 *
 * @details Implements high-performance message serialization with zero-copy support
 *          for inter-process communication. Uses SIMD for parallel processing and supports
 *          multiple formats with optional compression and encryption.
 *
 * Key features:
 * - Zero-copy serialization for same-arena actors
 * - SIMD-accelerated compression and encryption
 * - Multiple formats: binary, MessagePack, JSON, custom
 * - Platform-native shared memory for zero-copy
 * - Architecture-specific CRC and hash functions
 * - Lock-free serialization queues
 * - Hardware-accelerated AES-GCM (AES-NI, ARMv8 Crypto)
 * - Portable atomic operations (C11 or compiler builtins)
 *
 * @author OverLab Group
 * @date 2026
 *
 * 
 * 
 */

#ifndef OL_ACTOR_SERIALIZATION_H
#define OL_ACTOR_SERIALIZATION_H

#include "ol_common.h"
#include "ol_actor_process.h"      /* for ol_pid_t */
#include "ol_actor_isolation.h"     /* for ol_arena_t, ol_zero_copy_buffer_t */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>                 /* for static_assert */

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Serialization Types ==================== */

/**
 * @brief Serialization format
 */
typedef enum {
    OL_SERIALIZE_BINARY      = 0,  /**< Raw binary format */
    OL_SERIALIZE_MESSAGEPACK = 1,  /**< MessagePack format */
    OL_SERIALIZE_JSON        = 2,  /**< JSON format */
    OL_SERIALIZE_CUSTOM      = 3,  /**< Custom format with callbacks */
    OL_SERIALIZE_PROTOBUF    = 4,  /**< Protocol Buffers */
    OL_SERIALIZE_CAPNPROTO   = 5,  /**< Cap'n Proto (zero-copy) */
} ol_serialize_format_t;

/**
 * @brief Serialization flags
 */
typedef enum {
    OL_SERIALIZE_COMPRESS    = 1 << 0,  /**< Enable compression */
    OL_SERIALIZE_ENCRYPT     = 1 << 1,  /**< Enable encryption */
    OL_SERIALIZE_VALIDATE    = 1 << 2,  /**< Add integrity checks */
    OL_SERIALIZE_SHALLOW     = 1 << 3,  /**< Shallow copy (zero-copy) */
    OL_SERIALIZE_NON_TEMP    = 1 << 4,  /**< Non-temporal store */
    OL_SERIALIZE_PREFETCH    = 1 << 5,  /**< Prefetch during serialization */
} ol_serialize_flags_t;

/**
 * @brief Compression algorithm
 */
typedef enum {
    OL_COMPRESS_NONE         = 0,  /**< No compression */
    OL_COMPRESS_LZ4          = 1,  /**< LZ4 fast compression */
    OL_COMPRESS_LZ4HC        = 2,  /**< LZ4 high compression */
    OL_COMPRESS_ZSTD         = 3,  /**< Zstandard compression */
    OL_COMPRESS_SNAPPY       = 4,  /**< Snappy compression */
} ol_compress_algorithm_t;

/**
 * @brief Encryption algorithm
 */
typedef enum {
    OL_ENCRYPT_NONE          = 0,  /**< No encryption */
    OL_ENCRYPT_AES256_GCM    = 1,  /**< AES-256-GCM authenticated encryption */
    OL_ENCRYPT_CHACHA20      = 2,  /**< ChaCha20-Poly1305 */
    OL_ENCRYPT_XCHACHA20     = 3,  /**< XChaCha20-Poly1305 */
} ol_encrypt_algorithm_t;

/* ==================== Serialized Message Structure ==================== */

/**
 * @brief Serialized message header (packed for wire format)
 */
typedef struct OL_PACKED {
    uint32_t magic;             /**< Magic number: 0x4F4C4143 ('OLAC') */
    uint16_t version;           /**< Format version */
    uint16_t format;            /**< Serialization format */
    uint32_t flags;             /**< Serialization flags */
    uint64_t checksum;          /**< Data checksum (CRC64 or SHA256) */
    uint64_t timestamp;         /**< Creation timestamp (ns since epoch) */
    ol_pid_t sender_pid;        /**< Sender process ID */
    ol_pid_t receiver_pid;      /**< Receiver process ID */
    uint32_t data_size;         /**< Original data size */
    uint32_t compressed_size;   /**< Compressed size (0 if not compressed) */
    uint32_t encrypted_size;    /**< Encrypted size (0 if not encrypted) */
    uint8_t iv[16];             /**< Initialization vector for encryption */
    uint8_t auth_tag[16];       /**< Authentication tag (GCM/Poly1305) */
    uint8_t algorithm;          /**< Compression/encryption algorithm */
    uint8_t reserved[7];        /**< Reserved for future use */
} ol_serialize_header_t;

static_assert(sizeof(ol_serialize_header_t) == 128,
              "Serialization header must be 128 bytes");

/**
 * @brief Complete serialized message
 */
typedef struct ol_serialized_msg {
    ol_serialize_header_t header;  /**< Message header */
    uint8_t* data;                 /**< Message data (after header) */
    size_t size;                   /**< Total size (header + data) */
    ol_arena_t* arena;             /**< Source arena (for zero-copy) */
    uint32_t ref_count;            /**< Reference count for sharing */
} ol_serialized_msg_t;

/* ==================== Serialization Callbacks ==================== */

/**
 * @brief Custom serialization callback
 * @param data Data to serialize
 * @param size Pointer to data size (input/output)
 * @return Serialized data buffer
 */
typedef void* (*ol_serialize_callback)(void* data, size_t* size);

/**
 * @brief Custom deserialization callback
 * @param data Serialized data
 * @param size Data size
 * @return Deserialized data
 */
typedef void* (*ol_deserialize_callback)(const void* data, size_t size);

/**
 * @brief Serialization callbacks for custom format
 */
typedef struct ol_serialize_callbacks {
    ol_serialize_callback serialize;    /**< Serialization function */
    ol_deserialize_callback deserialize; /**< Deserialization function */
    void* user_data;                    /**< User data passed to callbacks */
} ol_serialize_callbacks_t;

/* ==================== Serialization API ==================== */

/**
 * @brief Serialize data for inter-process transfer
 * @param data Data to serialize
 * @param size Data size
 * @param format Serialization format
 * @param flags Serialization flags
 * @param sender_pid Sender process ID
 * @param receiver_pid Receiver process ID
 * @return Serialized message, NULL on failure
 */
OL_API ol_serialized_msg_t* ol_serialize(const void* data, size_t size,
                                 ol_serialize_format_t format,
                                 uint32_t flags,
                                 ol_pid_t sender_pid,
                                 ol_pid_t receiver_pid) OL_MALLOC_LIKE OL_WARN_UNUSED_RESULT;

/**
 * @brief Serialize with zero-copy optimization
 * @param arena Source arena (for zero-copy)
 * @param data Data to serialize
 * @param size Data size
 * @param format Serialization format
 * @param flags Serialization flags
 * @param sender_pid Sender process ID
 * @param receiver_pid Receiver process ID
 * @return Serialized message with zero-copy reference, NULL on failure
 */
OL_API ol_serialized_msg_t* ol_serialize_zero_copy(ol_arena_t* arena,
                                           const void* data, size_t size,
                                           ol_serialize_format_t format,
                                           uint32_t flags,
                                           ol_pid_t sender_pid,
                                           ol_pid_t receiver_pid) OL_MALLOC_LIKE OL_WARN_UNUSED_RESULT;

/**
 * @brief Deserialize message back to original data
 * @param msg Serialized message
 * @param out_data Output data pointer
 * @param out_size Output data size
 * @return 0 on success, -1 on error
 */
OL_API int ol_deserialize(const ol_serialized_msg_t* msg,
                  void** out_data, size_t* out_size);

/**
 * @brief Deserialize with zero-copy (reference counted)
 * @param msg Serialized message
 * @param out_buffer Output zero-copy buffer
 * @return 0 on success, -1 on error
 */
OL_API int ol_deserialize_zero_copy(const ol_serialized_msg_t* msg,
                            ol_zero_copy_buffer_t** out_buffer);

/**
 * @brief Free serialized message
 * @param msg Message to free
 */
OL_API void ol_serialize_free(ol_serialized_msg_t* msg);

/**
 * @brief Clone serialized message (increment ref count for zero-copy)
 * @param msg Message to clone
 * @return Cloned message
 */
OL_API ol_serialized_msg_t* ol_serialize_clone(const ol_serialized_msg_t* msg) OL_MALLOC_LIKE;

/**
 * @brief Get message format
 * @param msg Serialized message
 * @return Format enum value
 */
OL_API ol_serialize_format_t ol_serialize_get_format(const ol_serialized_msg_t* msg) OL_PURE;

/**
 * @brief Get total message size
 * @param msg Serialized message
 * @return Size in bytes
 */
OL_API size_t ol_serialize_get_size(const ol_serialized_msg_t* msg) OL_PURE;

/**
 * @brief Validate message integrity
 * @param msg Message to validate
 * @return true if valid, false otherwise
 */
OL_API bool ol_serialize_validate(const ol_serialized_msg_t* msg) OL_PURE;

/**
 * @brief Set custom serialization callbacks
 * @param callbacks Callback structure (NULL to clear)
 */
OL_API void ol_serialize_set_callbacks(const ol_serialize_callbacks_t* callbacks);

/* ==================== Compression API ==================== */

/**
 * @brief Compress data with SIMD acceleration
 * @param data Data to compress
 * @param size Data size
 * @param algorithm Compression algorithm
 * @param out_size Output compressed size
 * @return Compressed data, NULL on failure
 */
OL_API void* ol_compress(const void* data, size_t size,
                 ol_compress_algorithm_t algorithm,
                 size_t* out_size) OL_MALLOC_LIKE;

/**
 * @brief Decompress data
 * @param compressed Compressed data
 * @param compressed_size Compressed size
 * @param original_size Original uncompressed size
 * @param algorithm Compression algorithm
 * @param out_size Output decompressed size
 * @return Decompressed data, NULL on failure
 */
OL_API void* ol_decompress(const void* compressed, size_t compressed_size,
                   size_t original_size, ol_compress_algorithm_t algorithm,
                   size_t* out_size) OL_MALLOC_LIKE;

/**
 * @brief SIMD-accelerated LZ4 compression
 * @param src Source data
 * @param src_size Source size
 * @param dst Destination buffer
 * @param dst_capacity Destination capacity
 * @return Compressed size, 0 on error
 */
OL_API size_t ol_compress_lz4_fast(const void* src, size_t src_size,
                           void* dst, size_t dst_capacity) OL_HOT;

/**
 * @brief SIMD-accelerated LZ4 decompression
 * @param src Compressed data
 * @param compressed_size Compressed size
 * @param dst Destination buffer
 * @param dst_capacity Destination capacity
 * @return Decompressed size, 0 on error
 */
OL_API size_t ol_decompress_lz4_fast(const void* src, size_t compressed_size,
                             void* dst, size_t dst_capacity) OL_HOT;

/* ==================== Encryption API ==================== */

/**
 * @brief Encrypt data with authenticated encryption
 * @param data Data to encrypt
 * @param size Data size
 * @param key Encryption key (algorithm-specific size)
 * @param key_len Key length
 * @param algorithm Encryption algorithm
 * @param out_size Output encrypted size
 * @param iv Output initialization vector (16 bytes)
 * @param auth_tag Output authentication tag (16 bytes)
 * @return Encrypted data, NULL on failure
 */
OL_API void* ol_encrypt(const void* data, size_t size,
                const uint8_t* key, size_t key_len,
                ol_encrypt_algorithm_t algorithm,
                size_t* out_size, uint8_t iv[16],
                uint8_t auth_tag[16]) OL_MALLOC_LIKE;

/**
 * @brief Decrypt data with authentication
 * @param encrypted Encrypted data
 * @param encrypted_size Encrypted size
 * @param key Decryption key
 * @param key_len Key length
 * @param algorithm Encryption algorithm
 * @param iv Initialization vector
 * @param auth_tag Authentication tag
 * @param out_size Output decrypted size
 * @return Decrypted data, NULL on failure
 */
OL_API void* ol_decrypt(const void* encrypted, size_t encrypted_size,
                const uint8_t* key, size_t key_len,
                ol_encrypt_algorithm_t algorithm,
                const uint8_t iv[16], const uint8_t auth_tag[16],
                size_t* out_size) OL_MALLOC_LIKE;

/**
 * @brief Generate cryptographically secure random bytes
 * @param buffer Output buffer
 * @param size Number of bytes to generate
 * @return 0 on success, -1 on failure
 */
OL_API int ol_random_bytes(void* buffer, size_t size);

/* ==================== Checksum and Hash Functions ==================== */

/**
 * @brief Compute CRC32C with hardware acceleration
 * @param data Input data
 * @param size Data size
 * @param seed Initial CRC value
 * @return CRC32C checksum
 */
OL_API uint32_t ol_crc32c(const void* data, size_t size, uint32_t seed) OL_PURE;

/**
 * @brief Compute CRC64 with hardware acceleration
 * @param data Input data
 * @param size Data size
 * @param seed Initial CRC value
 * @return CRC64 checksum
 */
OL_API uint64_t ol_crc64(const void* data, size_t size, uint64_t seed) OL_PURE;

/**
 * @brief Compute SHA256 hash with SIMD acceleration
 * @param data Input data
 * @param size Data size
 * @param out_hash Output hash (32 bytes)
 */
OL_API void ol_sha256(const void* data, size_t size, uint8_t out_hash[32]) OL_PURE;

/**
 * @brief Compute fast 64-bit hash for message deduplication
 * @param data Input data
 * @param size Data size
 * @param seed Hash seed
 * @return 64-bit hash value
 */
OL_API uint64_t ol_hash_fast64(const void* data, size_t size, uint64_t seed) OL_PURE;

/* ==================== SIMD-Optimized Memory Operations ==================== */

/**
 * @brief SIMD-accelerated memory copy with non-temporal hints
 * @param dest Destination (must be 64-byte aligned)
 * @param src Source (must be 64-byte aligned)
 * @param n Number of bytes (multiple of 64)
 * @return dest
 */
OL_API void* ol_memcpy_simd(void* OL_RESTRICT dest, const void* OL_RESTRICT src,
                    size_t n) OL_HOT;

/**
 * @brief SIMD-accelerated memory compare
 * @param s1 First buffer (must be 64-byte aligned)
 * @param s2 Second buffer (must be 64-byte aligned)
 * @param n Number of bytes (multiple of 64)
 * @return 0 if equal, non-zero otherwise
 */
OL_API int ol_memcmp_simd(const void* s1, const void* s2, size_t n) OL_PURE OL_HOT;

/**
 * @brief SIMD-accelerated memory set
 * @param dest Destination (must be 64-byte aligned)
 * @param c Value to set
 * @param n Number of bytes (multiple of 64)
 * @return dest
 */
OL_API void* ol_memset_simd(void* dest, int c, size_t n) OL_HOT;

/* ==================== Zero-Copy Serialization Queue ==================== */

/**
 * @brief Zero-copy serialization queue for batch processing
 */
typedef struct ol_serialize_queue ol_serialize_queue_t;

/**
 * @brief Create serialization queue
 * @param capacity Queue capacity in messages
 * @param arena Memory arena (optional)
 * @return New queue, NULL on failure
 */
OL_API ol_serialize_queue_t* ol_serialize_queue_create(size_t capacity,
                                               ol_arena_t* arena) OL_MALLOC_LIKE;

/**
 * @brief Enqueue message for serialization
 * @param queue Serialization queue
 * @param msg Message to enqueue
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on timeout, -2 on error
 */
OL_API int ol_serialize_queue_enqueue(ol_serialize_queue_t* queue,
                              ol_serialized_msg_t* msg,
                              int timeout_ms);

/**
 * @brief Dequeue serialized message
 * @param queue Serialization queue
 * @param timeout_ms Timeout in milliseconds
 * @return Dequeued message, NULL on timeout or error
 */
OL_API ol_serialized_msg_t* ol_serialize_queue_dequeue(ol_serialize_queue_t* queue,
                                               int timeout_ms);

/**
 * @brief Batch enqueue multiple messages
 * @param queue Serialization queue
 * @param msgs Array of messages
 * @param count Number of messages
 * @param timeout_ms Timeout in milliseconds
 * @return Number of messages successfully enqueued
 */
OL_API size_t ol_serialize_queue_enqueue_batch(ol_serialize_queue_t* queue,
                                       ol_serialized_msg_t** msgs,
                                       size_t count, int timeout_ms);

/**
 * @brief Batch dequeue multiple messages
 * @param queue Serialization queue
 * @param out_msgs Output array for messages
 * @param max_count Maximum number to dequeue
 * @param timeout_ms Timeout in milliseconds
 * @return Number of messages dequeued
 */
OL_API size_t ol_serialize_queue_dequeue_batch(ol_serialize_queue_t* queue,
                                       ol_serialized_msg_t** out_msgs,
                                       size_t max_count, int timeout_ms);

/**
 * @brief Destroy serialization queue
 * @param queue Queue to destroy
 */
OL_API void ol_serialize_queue_destroy(ol_serialize_queue_t* queue);

/* ==================== MessagePack Serialization ==================== */

/**
 * @brief MessagePack serializer context
 */
typedef struct ol_msgpack_serializer ol_msgpack_serializer_t;

/**
 * @brief Create MessagePack serializer
 * @param arena Memory arena (optional)
 * @return New serializer, NULL on failure
 */
OL_API ol_msgpack_serializer_t* ol_msgpack_serializer_create(ol_arena_t* arena) OL_MALLOC_LIKE;

/**
 * @brief Serialize value to MessagePack format
 * @param serializer Serializer instance
 * @param data Data to serialize
 * @param size Data size
 * @param type Data type identifier
 * @return Serialized MessagePack data, NULL on failure
 */
OL_API void* ol_msgpack_serialize(ol_msgpack_serializer_t* serializer,
                          const void* data, size_t size, int type);

/**
 * @brief Deserialize MessagePack data
 * @param data MessagePack data
 * @param size Data size
 * @param out_type Output data type
 * @param out_size Output data size
 * @return Deserialized data, NULL on failure
 */
OL_API void* ol_msgpack_deserialize(const void* data, size_t size,
                            int* out_type, size_t* out_size);

/* ==================== Platform-Specific Optimizations ==================== */

#if OL_ARCH_X86_64 && defined(__AES__) && defined(__PCLMUL__)

/**
 * @brief AES-GCM encryption using Intel AES-NI and PCLMULQDQ
 */
OL_API void ol_aes_gcm_encrypt_x86(const void* plaintext, size_t plaintext_len,
                           const uint8_t key[32], const uint8_t iv[12],
                           void* ciphertext, uint8_t auth_tag[16]);

/**
 * @brief AES-GCM decryption using Intel AES-NI
 */
OL_API int ol_aes_gcm_decrypt_x86(const void* ciphertext, size_t ciphertext_len,
                          const uint8_t key[32], const uint8_t iv[12],
                          const uint8_t auth_tag[16], void* plaintext);

#elif OL_ARCH_ARM64 && defined(__ARM_FEATURE_AES) && defined(__ARM_FEATURE_PMULL)

/**
 * @brief AES-GCM encryption using ARM Crypto extensions
 */
OL_API void ol_aes_gcm_encrypt_arm(const void* plaintext, size_t plaintext_len,
                           const uint8_t key[32], const uint8_t iv[12],
                           void* ciphertext, uint8_t auth_tag[16]);

/**
 * @brief AES-GCM decryption using ARM Crypto extensions
 */
OL_API int ol_aes_gcm_decrypt_arm(const void* ciphertext, size_t ciphertext_len,
                          const uint8_t key[32], const uint8_t iv[12],
                          const uint8_t auth_tag[16], void* plaintext);

#endif

/* ==================== Serialization Statistics ==================== */

/**
 * @brief Serialization performance statistics
 */
typedef struct ol_serialize_stats {
    uint64_t serialize_count;          /**< Number of serializations */
    uint64_t deserialize_count;        /**< Number of deserializations */
    uint64_t total_serialized_bytes;   /**< Total bytes serialized */
    uint64_t total_deserialized_bytes; /**< Total bytes deserialized */
    uint64_t compression_saved_bytes;  /**< Bytes saved by compression */
    uint64_t zero_copy_bytes;          /**< Bytes transferred zero-copy */
    uint64_t simd_optimized_ops;       /**< Number of SIMD-optimized operations */
    double avg_serialize_time_ns;      /**< Average serialization time (ns) */
    double avg_deserialize_time_ns;    /**< Average deserialization time (ns) */
} ol_serialize_stats_t;

/**
 * @brief Get serialization statistics
 * @param stats Output statistics structure
 */
OL_API void ol_serialize_get_stats(ol_serialize_stats_t* stats);

/**
 * @brief Reset serialization statistics
 */
OL_API void ol_serialize_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* OL_ACTOR_SERIALIZATION_H */