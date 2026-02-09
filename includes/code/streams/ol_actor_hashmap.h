/**
 * @file ol_hashmap.h
 * @brief Simple hash map implementation for internal use in actor system
 * 
 * @details Provides a basic hash map implementation for use within the actor
 * system for tracking pending ask requests, process registries, and other
 * internal data structures. Features include:
 * - Fixed-size bucket array with separate chaining
 * - Support for arbitrary key types (via byte arrays)
 * - Optional value destructor for automatic cleanup
 * - Thread-unsafe (caller must synchronize if needed)
 * 
 * @note This is an internal implementation and may not be suitable for
 *       general-purpose use. For production hash maps, consider using
 *       a more robust library.
 * 
 * @author OverLab Group
 * @version 1.3.0
 * @date 2026
 */

#ifndef OL_HASHMAP_H
#define OL_HASHMAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Opaque hash map structure
 * 
 * @details The actual implementation details are hidden in the .c file.
 * Users interact with the hash map through the provided API functions.
 */
typedef struct ol_hashmap ol_hashmap_t;

/**
 * @brief Create a new hash map
 * 
 * @param capacity Initial bucket capacity (rounded up to power of 2)
 * @param value_destructor Function to destroy values when removed (can be NULL)
 * @return ol_hashmap_t* New hash map instance, NULL on failure
 * 
 * @note The actual capacity will be the next power of two >= requested capacity.
 *       If value_destructor is provided, it will be called for each value when
 *       the hash map is destroyed or when entries are removed.
 */
ol_hashmap_t* ol_hashmap_create(size_t capacity, void (*value_destructor)(void*));

/**
 * @brief Destroy hash map and all entries
 * 
 * @param map Hash map to destroy (can be NULL)
 * 
 * @details Frees all memory associated with the hash map, including:
 * - All key-value entries
 * - Internal bucket array
 * - Hash map structure itself
 * 
 * @note If a value_destructor was provided during creation, it will be
 *       called for each value before freeing the entry.
 */
void ol_hashmap_destroy(ol_hashmap_t* map);

/**
 * @brief Insert or update key-value pair
 * 
 * @param map Hash map to modify
 * @param key Pointer to key data
 * @param key_size Size of key data in bytes
 * @param value Value to associate with key (ownership retained by caller)
 * @return bool true on success, false on failure (e.g., out of memory)
 * 
 * @details If the key already exists, the old value will be replaced
 * and the old value destructor will be called (if provided).
 * 
 * @warning The key data is copied internally, but the value pointer
 *          is stored directly. The hash map does NOT take ownership
 *          of the value memory unless a value_destructor is provided.
 */
bool ol_hashmap_put(ol_hashmap_t* map, const void* key, size_t key_size, void* value);

/**
 * @brief Get value by key
 * 
 * @param map Hash map to search
 * @param key Pointer to key data
 * @param key_size Size of key data in bytes
 * @return void* Associated value, or NULL if key not found
 * 
 * @note Returns NULL if key not found or if map is NULL.
 *       The returned value is the original pointer stored with ol_hashmap_put().
 */
void* ol_hashmap_get(const ol_hashmap_t* map, const void* key, size_t key_size);

/**
 * @brief Remove key-value pair
 * 
 * @param map Hash map to modify
 * @param key Pointer to key data
 * @param key_size Size of key data in bytes
 * @return bool true if key was found and removed, false otherwise
 * 
 * @details If the key exists and is removed:
 * - The value destructor will be called (if provided during creation)
 * - The entry memory will be freed
 * - Returns true
 * 
 * If the key doesn't exist, returns false.
 */
bool ol_hashmap_remove(ol_hashmap_t* map, const void* key, size_t key_size);

/**
 * @brief Get number of entries in hash map
 * 
 * @param map Hash map to examine
 * @return size_t Number of key-value pairs in the map, 0 if map is NULL
 */
size_t ol_hashmap_size(const ol_hashmap_t* map);

/**
 * @brief Clear all entries in hash map
 * 
 * @param map Hash map to clear
 * 
 * @details Removes all key-value pairs from the hash map.
 * If a value_destructor was provided during creation, it will be
 * called for each value before removal.
 * 
 * @note The hash map remains usable after clearing. The bucket array
 *       capacity is not reduced by this operation.
 */
void ol_hashmap_clear(ol_hashmap_t* map);

#endif /* OL_HASHMAP_H */
