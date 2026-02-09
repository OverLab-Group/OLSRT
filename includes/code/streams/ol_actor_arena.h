/**
 * @file ol_arena.h
 * @brief Memory arena for process isolation in actor system
 * 
 * @details Each process gets its own memory arena to ensure complete isolation.
 * Memory allocated in one arena cannot be accessed from another process.
 * This prevents memory corruption and enables "allow it to crash" philosophy
 * similar to Erlang/OTP.
 * 
 * Key features:
 * - Complete memory isolation between processes
 * - Fast bump allocator with fallback to free lists
 * - Guard pages for detecting buffer overflows
 * - Memory usage statistics and monitoring
 * - Shared arenas for controlled inter-process communication
 * - Automatic arena expansion when needed
 * 
 * @note Arenas provide a region-based memory management strategy that
 *       reduces fragmentation and improves cache locality compared to
 *       general-purpose malloc/free.
 * 
 * @author OverLab Group
 * @version 1.3.0
 * @date 2026
 */

#ifndef OL_ARENA_H
#define OL_ARENA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Opaque memory arena structure
 * 
 * @details Actual implementation is hidden. Users interact with arenas
 * through the provided API functions.
 */
typedef struct ol_arena ol_arena_t;

/**
 * @brief Arena statistics structure for monitoring and debugging
 */
typedef struct {
    size_t total_size;     /**< Total arena size in bytes */
    size_t used_size;      /**< Currently allocated/used bytes */
    size_t alloc_count;    /**< Number of active allocations */
    size_t free_count;     /**< Number of free blocks in free list */
    size_t peak_usage;     /**< Peak memory usage reached */
} ol_arena_stats_t;

/**
 * @brief Create a new memory arena
 * 
 * @param size Initial arena size in bytes (0 = use default size)
 * @param is_shared If true, arena can be shared between processes
 * @return ol_arena_t* New arena instance, NULL on failure
 * 
 * @details Creates a memory arena with the specified initial size.
 * The arena is page-aligned and surrounded by guard pages for
 * overflow detection when supported by the platform.
 * 
 * @note Default size is typically 4MB. Arena will automatically
 *       expand if more memory is needed (if possible).
 */
ol_arena_t* ol_arena_create(size_t size, bool is_shared);

/**
 * @brief Destroy arena and free all memory
 * 
 * @param arena Arena to destroy (can be NULL)
 * 
 * @details Frees all memory associated with the arena, including:
 * - All allocated blocks (without calling destructors)
 * - Internal metadata and free lists
 * - Guard pages
 * - Arena structure itself
 * 
 * @warning Any pointers to memory within the arena become invalid
 *          after this call. Caller must ensure no dangling references.
 */
void ol_arena_destroy(ol_arena_t* arena);

/**
 * @brief Allocate memory from arena
 * 
 * @param arena Arena to allocate from
 * @param size Size to allocate in bytes
 * @return void* Allocated memory, NULL on failure
 * 
 * @details Allocates memory from the arena's pool. The memory is
 * aligned to 8 bytes and includes guard patterns for overflow
 * detection in debug builds.
 * 
 * @note Allocation is O(1) in the common case (bump allocation).
 *       Falls back to free list if previous allocations were freed.
 * @warning Memory is not zero-initialized. Use calloc() pattern if needed.
 */
void* ol_arena_alloc(ol_arena_t* arena, size_t size);

/**
 * @brief Allocate aligned memory from arena
 * 
 * @param arena Arena to allocate from
 * @param alignment Alignment requirement (must be power of 2)
 * @param size Size to allocate in bytes
 * @return void* Allocated memory, NULL on failure
 * 
 * @details Similar to ol_arena_alloc() but with specified alignment.
 * Useful for SIMD instructions, cache lines, or hardware requirements.
 * 
 * @note Alignment must be a power of two. Common alignments:
 *       16 (SSE), 32 (AVX), 64 (cache line).
 */
void* ol_arena_alloc_aligned(ol_arena_t* arena, size_t alignment, size_t size);

/**
 * @brief Free memory allocated from arena
 * 
 * @param arena Arena that memory belongs to
 * @param ptr Pointer to free (must have been allocated from this arena)
 * 
 * @details Returns memory to the arena's free list for reuse.
 * The memory is not immediately returned to the OS but becomes
 * available for future allocations within the same arena.
 * 
 * @note Freeing is O(1). Memory is added to a free list and can
 *       be coalesced with adjacent free blocks.
 * @warning Calling with NULL pointer or pointer from different arena
 *          results in undefined behavior.
 */
void ol_arena_free(ol_arena_t* arena, void* ptr);

/**
 * @brief Reset arena (free all allocations)
 * 
 * @param arena Arena to reset
 * 
 * @details Releases all allocations in the arena, resetting it to
 * empty state. Memory remains allocated from the OS but is marked
 * as available for new allocations.
 * 
 * @note This is much faster than freeing each allocation individually.
 *       Useful for batch processing or phase-based memory management.
 */
void ol_arena_reset(ol_arena_t* arena);

/**
 * @brief Get arena statistics
 * 
 * @param arena Arena to get stats for
 * @param stats Output structure to fill with statistics
 * @return int OL_SUCCESS on success, OL_ERROR on error (NULL parameters)
 */
int ol_arena_get_stats(const ol_arena_t* arena, ol_arena_stats_t* stats);

/**
 * @brief Check if pointer belongs to arena
 * 
 * @param arena Arena to check against
 * @param ptr Pointer to check
 * @return bool true if pointer is within arena's memory range, false otherwise
 * 
 * @note Useful for debugging and validating pointer ownership.
 */
bool ol_arena_contains(const ol_arena_t* arena, const void* ptr);

/**
 * @brief Get arena total size
 * 
 * @param arena Arena instance
 * @return size_t Total size in bytes (including overhead), 0 if arena is NULL
 */
size_t ol_arena_total_size(const ol_arena_t* arena);

/**
 * @brief Get arena used size
 * 
 * @param arena Arena instance
 * @return size_t Currently used size in bytes, 0 if arena is NULL
 */
size_t ol_arena_used_size(const ol_actor_t* actor);

/**
 * @brief Expand arena if possible
 * 
 * @param arena Arena to expand
 * @param additional_size Additional size needed in bytes
 * @return int OL_SUCCESS on success, OL_ERROR on failure
 * 
 * @details Attempts to expand the arena's memory pool to accommodate
 * more allocations. May involve remapping memory or allocating new regions.
 * 
 * @note On success, existing allocations remain valid. On failure,
 *       arena remains unchanged.
 */
int ol_arena_expand(ol_arena_t* arena, size_t additional_size);

/**
 * @brief Create a sub-arena within parent arena
 * 
 * @param parent Parent arena to allocate sub-arena from
 * @param size Sub-arena size in bytes
 * @return ol_arena_t* New sub-arena, NULL on failure
 * 
 * @details Creates a nested arena allocated from the parent arena.
 * Sub-arenas share the parent's memory pool but maintain separate
 * allocation tracking.
 * 
 * @note Useful for hierarchical memory management or temporary scopes.
 *       Destroying parent arena also destroys all sub-arenas.
 */
ol_arena_t* ol_arena_create_sub(ol_arena_t* parent, size_t size);

/**
 * @brief Check if arena is shared between processes
 * 
 * @param arena Arena instance
 * @return bool true if arena is shared, false otherwise or if arena is NULL
 * 
 * @note Shared arenas require platform-specific shared memory mechanisms
 *       and proper synchronization.
 */
bool ol_arena_is_shared(const ol_arena_t* arena);

#endif /* OL_ARENA_H */
