/**
 * @file ol_actor_hashmap.c
 * @brief Simple hash map implementation for internal use in actor system.
 *
 * @details
 * Provides a small chained-bucket hash map. Keys are arbitrary byte
 * sequences; values are opaque pointers. Optional value_destructor is
 * invoked on entry removal and on map destruction.
 *
 * Thread-safety: NOT thread-safe. Callers must synchronize if a single
 * map instance is shared between threads. This is intentional, since
 * the actor/supervisor code already holds its own mutexes.
 *
 * @author OverLab Group
 * @version 1.3.1
 * @date 2026
 */

#include "ol_actor_hashmap.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ==================== Internal structures ==================== */

typedef struct ol_hashmap_entry {
    void* key;       /* Heap-allocated copy of the key bytes */
    size_t key_size; /* Length of key in bytes */
    void* value;     /* User pointer (ownership retained) */
    struct ol_hashmap_entry* next;
} ol_hashmap_entry_t;

struct ol_hashmap {
    ol_hashmap_entry_t** buckets;
    size_t capacity; /* Number of buckets */
    size_t size;     /* Number of entries */
    void (*value_destructor)(void*);
};

/* ==================== Hash function ==================== */

/**
 * FNV-1a 64-bit hash. Fast, deterministic, and good enough for the
 * small maps used inside the actor and supervisor subsystems.
 */
static uint64_t fnv1a_64(const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < size; i++) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/**
 * Round up to the next power of two (>= 8).
 */
static size_t next_pow2(size_t n) {
    if (n < 8)
        return 8;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    return n + 1;
}

/* ==================== Public API ==================== */

ol_hashmap_t* ol_hashmap_create(size_t capacity,
                                void (*value_destructor)(void*)) {
    ol_hashmap_t* map = (ol_hashmap_t*)calloc(1, sizeof(*map));
    if (!map)
        return NULL;

    capacity = next_pow2(capacity ? capacity : 16);
    map->buckets =
        (ol_hashmap_entry_t**)calloc(capacity, sizeof(ol_hashmap_entry_t*));
    if (!map->buckets) {
        free(map);
        return NULL;
    }
    map->capacity = capacity;
    map->size = 0;
    map->value_destructor = value_destructor;
    return map;
}

void ol_hashmap_destroy(ol_hashmap_t* map) {
    if (!map)
        return;
    for (size_t i = 0; i < map->capacity; i++) {
        ol_hashmap_entry_t* e = map->buckets[i];
        while (e) {
            ol_hashmap_entry_t* next = e->next;
            if (map->value_destructor && e->value) {
                map->value_destructor(e->value);
            }
            free(e->key);
            free(e);
            e = next;
        }
    }
    free(map->buckets);
    free(map);
}

bool ol_hashmap_put(ol_hashmap_t* map,
                    const void* key,
                    size_t key_size,
                    void* value) {
    if (!map || !key || key_size == 0)
        return false;

    size_t idx = (size_t)(fnv1a_64(key, key_size) & (map->capacity - 1));

    /* Search for existing key */
    for (ol_hashmap_entry_t* e = map->buckets[idx]; e; e = e->next) {
        if (e->key_size == key_size && memcmp(e->key, key, key_size) == 0) {
            /* Replace value; invoke old destructor if any */
            if (map->value_destructor && e->value && e->value != value) {
                map->value_destructor(e->value);
            }
            e->value = value;
            return true;
        }
    }

    /* New entry */
    ol_hashmap_entry_t* e = (ol_hashmap_entry_t*)malloc(sizeof(*e));
    if (!e)
        return false;
    e->key = malloc(key_size);
    if (!e->key) {
        free(e);
        return false;
    }
    memcpy(e->key, key, key_size);
    e->key_size = key_size;
    e->value = value;
    e->next = map->buckets[idx];
    map->buckets[idx] = e;
    map->size++;
    return true;
}

void* ol_hashmap_get(const ol_hashmap_t* map,
                     const void* key,
                     size_t key_size) {
    if (!map || !key || key_size == 0)
        return NULL;

    size_t idx = (size_t)(fnv1a_64(key, key_size) & (map->capacity - 1));
    for (ol_hashmap_entry_t* e = map->buckets[idx]; e; e = e->next) {
        if (e->key_size == key_size && memcmp(e->key, key, key_size) == 0) {
            return e->value;
        }
    }
    return NULL;
}

bool ol_hashmap_remove(ol_hashmap_t* map, const void* key, size_t key_size) {
    if (!map || !key || key_size == 0)
        return false;

    size_t idx = (size_t)(fnv1a_64(key, key_size) & (map->capacity - 1));
    ol_hashmap_entry_t** pp = &map->buckets[idx];
    while (*pp) {
        ol_hashmap_entry_t* e = *pp;
        if (e->key_size == key_size && memcmp(e->key, key, key_size) == 0) {
            *pp = e->next;
            if (map->value_destructor && e->value) {
                map->value_destructor(e->value);
            }
            free(e->key);
            free(e);
            map->size--;
            return true;
        }
        pp = &e->next;
    }
    return false;
}

size_t ol_hashmap_size(const ol_hashmap_t* map) {
    return map ? map->size : 0;
}

void ol_hashmap_clear(ol_hashmap_t* map) {
    if (!map)
        return;
    for (size_t i = 0; i < map->capacity; i++) {
        ol_hashmap_entry_t* e = map->buckets[i];
        while (e) {
            ol_hashmap_entry_t* next = e->next;
            if (map->value_destructor && e->value) {
                map->value_destructor(e->value);
            }
            free(e->key);
            free(e);
            e = next;
        }
        map->buckets[i] = NULL;
    }
    map->size = 0;
}
