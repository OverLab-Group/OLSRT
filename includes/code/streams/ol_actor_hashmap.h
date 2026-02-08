/**
 * @file ol_hashmap.h
 * @brief Simple hash map implementation for internal use
 */

#ifndef OL_HASHMAP_H
#define OL_HASHMAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct ol_hashmap ol_hashmap_t;

/**
 * @brief Create a new hash map
 * 
 * @param capacity Initial capacity (power of 2 recommended)
 * @param value_destructor Function to destroy values (can be NULL)
 * @return ol_hashmap_t* New hash map
 */
ol_hashmap_t* ol_hashmap_create(size_t capacity, void (*value_destructor)(void*));

/**
 * @brief Destroy hash map and all entries
 * 
 * @param map Hash map to destroy
 */
void ol_hashmap_destroy(ol_hashmap_t* map);

/**
 * @brief Insert or update key-value pair
 * 
 * @param map Hash map
 * @param key Key pointer
 * @param key_size Key size in bytes
 * @param value Value pointer
 * @return bool True on success
 */
bool ol_hashmap_put(ol_hashmap_t* map, const void* key, size_t key_size, void* value);

/**
 * @brief Get value by key
 * 
 * @param map Hash map
 * @param key Key pointer
 * @param key_size Key size in bytes
 * @return void* Value or NULL if not found
 */
void* ol_hashmap_get(const ol_hashmap_t* map, const void* key, size_t key_size);

/**
 * @brief Remove key-value pair
 * 
 * @param map Hash map
 * @param key Key pointer
 * @param key_size Key size in bytes
 * @return bool True if key was found and removed
 */
bool ol_hashmap_remove(ol_hashmap_t* map, const void* key, size_t key_size);

/**
 * @brief Get number of entries in hash map
 * 
 * @param map Hash map
 * @return size_t Number of entries
 */
size_t ol_hashmap_size(const ol_hashmap_t* map);

/**
 * @brief Clear all entries in hash map
 * 
 * @param map Hash map
 */
void ol_hashmap_clear(ol_hashmap_t* map);

#endif /* OL_HASHMAP_H */
