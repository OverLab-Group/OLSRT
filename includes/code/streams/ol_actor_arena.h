/**
 * @file ol_arena.h
 * @brief Memory arena for process isolation
 * 
 * @details Each process gets its own memory arena to ensure complete isolation.
 * Memory allocated in one arena cannot be accessed from another process.
 * This prevents memory corruption and enables "allow it to crash" philosophy.
 */

#ifndef OL_ARENA_H
#define OL_ARENA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** Memory arena structure (opaque) */
typedef struct ol_arena ol_arena_t;

/** Arena statistics */
typedef struct {
    size_t total_size;     /**< Total arena size */
    size_t used_size;      /**< Used memory */
    size_t alloc_count;    /**< Number of allocations */
    size_t free_count;     /**< Number of frees */
    size_t peak_usage;     /**< Peak memory usage */
} ol_arena_stats_t;

/**
 * @brief Create a new memory arena
 * 
 * @param size Initial arena size (0 for default)
 * @param is_shared If true, arena can be shared between processes
 * @return ol_arena_t* New arena or NULL on failure
 */
ol_arena_t* ol_arena_create(size_t size, bool is_shared);

/**
 * @brief Destroy arena and free all memory
 * 
 * @param arena Arena to destroy
 */
void ol_arena_destroy(ol_arena_t* arena);

/**
 * @brief Allocate memory from arena
 * 
 * @param arena Arena to allocate from
 * @param size Size to allocate
 * @return void* Allocated memory or NULL on failure
 */
void* ol_arena_alloc(ol_arena_t* arena, size_t size);

/**
 * @brief Allocate aligned memory from arena
 * 
 * @param arena Arena to allocate from
 * @param alignment Alignment requirement (must be power of 2)
 * @param size Size to allocate
 * @return void* Allocated memory or NULL on failure
 */
void* ol_arena_alloc_aligned(ol_arena_t* arena, size_t alignment, size_t size);

/**
 * @brief Free memory allocated from arena
 * 
 * @param arena Arena that memory belongs to
 * @param ptr Pointer to free
 */
void ol_arena_free(ol_arena_t* arena, void* ptr);

/**
 * @brief Reset arena (free all allocations)
 * 
 * @param arena Arena to reset
 */
void ol_arena_reset(ol_arena_t* arena);

/**
 * @brief Get arena statistics
 * 
 * @param arena Arena to get stats for
 * @param stats Output statistics
 * @return int 0 on success, -1 on error
 */
int ol_arena_get_stats(const ol_arena_t* arena, ol_arena_stats_t* stats);

/**
 * @brief Check if pointer belongs to arena
 * 
 * @param arena Arena to check
 * @param ptr Pointer to check
 * @return bool True if pointer is in arena
 */
bool ol_arena_contains(const ol_arena_t* arena, const void* ptr);

/**
 * @brief Get arena total size
 * 
 * @param arena Arena instance
 * @return size_t Total size in bytes
 */
size_t ol_arena_total_size(const ol_arena_t* arena);

/**
 * @brief Get arena used size
 * 
 * @param arena Arena instance
 * @return size_t Used size in bytes
 */
size_t ol_arena_used_size(const ol_arena_t* arena);

/**
 * @brief Expand arena if possible
 * 
 * @param arena Arena to expand
 * @param additional_size Additional size needed
 * @return int 0 on success, -1 on error
 */
int ol_arena_expand(ol_arena_t* arena, size_t additional_size);

/**
 * @brief Create a sub-arena within parent arena
 * 
 * @param parent Parent arena
 * @param size Sub-arena size
 * @return ol_arena_t* New sub-arena
 */
ol_arena_t* ol_arena_create_sub(ol_arena_t* parent, size_t size);

/**
 * @brief Check if arena is shared
 * 
 * @param arena Arena instance
 * @return bool True if arena is shared
 */
bool ol_arena_is_shared(const ol_arena_t* arena);

#endif /* OL_ARENA_H */