/**
 * @file ol_arena.c
 * @brief Complete memory arena implementation for process isolation
 * @version 1.2.0
 * 
 * @details Implements per-process memory arenas that provide complete isolation
 * between processes. Each arena manages its own memory pool, preventing
 * cross-process memory corruption and enabling Erlang-style "let it crash" philosophy.
 * 
 * Features:
 * - Complete memory isolation between processes
 * - Fast bump allocator with fallback to free lists
 * - Guard pages for detecting buffer overflows
 * - Memory usage statistics and monitoring
 * - Shared arenas for controlled inter-process communication
 * - Automatic arena expansion when needed
 */

#define _GNU_SOURCE

#include "ol_actor_arena.h"
#include "ol_common.h"
#include "ol_lock_mutex.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(_WIN32)
    #include <windows.h>
    #define OL_PAGE_SIZE 4096
    #define OL_ALIGN_TO_PAGE(addr) (((uintptr_t)(addr) + OL_PAGE_SIZE - 1) & ~(OL_PAGE_SIZE - 1))
#else
    #include <unistd.h>
    #include <sys/mman.h>
    
    static size_t ol_get_page_size(void) {
        static size_t page_size = 0;
        if (page_size == 0) {
            page_size = sysconf(_SC_PAGESIZE);
        }
        return page_size;
    }
    #define OL_PAGE_SIZE ol_get_page_size()
    #define OL_ALIGN_TO_PAGE(addr) (((uintptr_t)(addr) + OL_PAGE_SIZE - 1) & ~(OL_PAGE_SIZE - 1))
#endif

/* ==================== Internal Structures ==================== */

/**
 * @brief Free list node for deallocated memory blocks
 */
typedef struct arena_free_node {
    size_t size;
    struct arena_free_node* next;
} arena_free_node_t;

/**
 * @brief Arena metadata header
 */
typedef struct arena_header {
    size_t total_size;          /**< Total arena size including header */
    size_t used_size;           /**< Currently used bytes */
    size_t allocated_blocks;    /**< Number of active allocations */
    size_t free_blocks;         /**< Number of blocks in free list */
    size_t peak_usage;          /**< Peak memory usage */
    bool is_shared;             /**< Whether arena can be shared between processes */
    uint64_t owner_pid;         /**< Owner process ID (0 for shared) */
    uint8_t guard_pattern[64];  /**< Guard pattern for detecting corruption */
} arena_header_t;

/**
 * @brief Allocation metadata (prepended to each allocation)
 */
typedef struct alloc_header {
    size_t size;                /**< Size of user data */
    uint32_t magic;             /**< Magic number for validation */
    uint8_t guard_start[16];    /**< Start guard pattern */
    /* User data follows immediately */
} alloc_header_t;

#define ALLOC_MAGIC 0xAFEA1234
#define GUARD_PATTERN 0xCC

/**
 * @brief Main arena structure
 */
struct ol_arena {
    arena_header_t* header;     /**< Arena metadata */
    void* memory_pool;          /**< Start of usable memory */
    size_t pool_size;           /**< Size of usable memory */
    
    /* Allocation management */
    arena_free_node_t* free_list;   /**< Free list for deallocated blocks */
    size_t free_list_size;          /**< Total size in free list */
    
    /* Guard pages */
    void* guard_before;         /**< Guard page before arena (read-only) */
    void* guard_after;          /**< Guard page after arena (read-only) */
    size_t guard_size;          /**< Size of each guard region */
    
    /* Synchronization */
    ol_mutex_t mutex;           /**< Mutex for thread-safe operations */
    
    /* Statistics */
    size_t total_allocations;   /**< Total allocations in arena lifetime */
    size_t total_frees;         /**< Total frees in arena lifetime */
    size_t expansion_count;     /**< Number of times arena expanded */
};

/* ==================== Internal Helper Functions ==================== */

/**
 * @brief Initialize guard pages around arena memory
 * 
 * @param arena Arena instance
 * @return int OL_SUCCESS on success, OL_ERROR on failure
 */
static int ol_arena_init_guards(ol_arena_t* arena) {
    if (!arena || !arena->header) {
        return OL_ERROR;
    }
    
    arena->guard_size = OL_PAGE_SIZE;
    
    /* Create guard page before arena */
#if defined(_WIN32)
    arena->guard_before = VirtualAlloc(NULL, arena->guard_size,
                                      MEM_RESERVE | MEM_COMMIT,
                                      PAGE_READONLY);
    if (!arena->guard_before) {
        return OL_ERROR;
    }
#else
    arena->guard_before = mmap(NULL, arena->guard_size,
                              PROT_READ,
                              MAP_PRIVATE | MAP_ANONYMOUS,
                              -1, 0);
    if (arena->guard_before == MAP_FAILED) {
        arena->guard_before = NULL;
        return OL_ERROR;
    }
#endif
    
    /* Create guard page after arena */
    void* arena_end = (char*)arena->memory_pool + arena->pool_size;
#if defined(_WIN32)
    arena->guard_after = VirtualAlloc(arena_end, arena->guard_size,
                                     MEM_RESERVE | MEM_COMMIT,
                                     PAGE_READONLY);
    if (!arena->guard_after) {
        VirtualFree(arena->guard_before, 0, MEM_RELEASE);
        return OL_ERROR;
    }
#else
    arena->guard_after = mmap(arena_end, arena->guard_size,
                             PROT_READ,
                             MAP_PRIVATE | MAP_ANONYMOUS,
                             -1, 0);
    if (arena->guard_after == MAP_FAILED) {
        arena->guard_after = NULL;
        munmap(arena->guard_before, arena->guard_size);
        return OL_ERROR;
    }
#endif
    
    return OL_SUCCESS;
}

/**
 * @brief Destroy guard pages
 * 
 * @param arena Arena instance
 */
static void ol_arena_destroy_guards(ol_arena_t* arena) {
    if (!arena) return;
    
    if (arena->guard_before) {
#if defined(_WIN32)
        VirtualFree(arena->guard_before, 0, MEM_RELEASE);
#else
        munmap(arena->guard_before, arena->guard_size);
#endif
        arena->guard_before = NULL;
    }
    
    if (arena->guard_after) {
#if defined(_WIN32)
        VirtualFree(arena->guard_after, 0, MEM_RELEASE);
#else
        munmap(arena->guard_after, arena->guard_size);
#endif
        arena->guard_after = NULL;
    }
}

/**
 * @brief Validate allocation header for corruption
 * 
 * @param header Allocation header to validate
 * @return int OL_SUCCESS if valid, OL_ERROR if corrupted
 */
static int ol_arena_validate_allocation(const alloc_header_t* header) {
    if (!header) return OL_ERROR;
    
    /* Check magic number */
    if (header->magic != ALLOC_MAGIC) {
        return OL_ERROR;
    }
    
    /* Check guard pattern */
    for (int i = 0; i < 16; i++) {
        if (header->guard_start[i] != GUARD_PATTERN) {
            return OL_ERROR;
        }
    }
    
    return OL_SUCCESS;
}

/**
 * @brief Find free block of at least specified size
 * 
 * @param arena Arena instance
 * @param size Minimum size needed
 * @return arena_free_node_t** Pointer to pointer to found block (for removal)
 */
static arena_free_node_t** ol_arena_find_free_block(ol_arena_t* arena, size_t size) {
    if (!arena || !arena->free_list) return NULL;
    
    arena_free_node_t** node_ptr = &arena->free_list;
    while (*node_ptr) {
        if ((*node_ptr)->size >= size) {
            return node_ptr;
        }
        node_ptr = &(*node_ptr)->next;
    }
    
    return NULL;
}

/**
 * @brief Split a free block if it's larger than needed
 * 
 * @param block Block to split
 * @param size Size needed
 */
static void ol_arena_split_free_block(arena_free_node_t* block, size_t size) {
    if (!block || block->size < size + sizeof(arena_free_node_t) + 16) {
        return; /* Not worth splitting */
    }
    
    size_t remaining = block->size - size;
    arena_free_node_t* new_block = (arena_free_node_t*)((char*)block + size);
    new_block->size = remaining;
    new_block->next = block->next;
    
    block->size = size;
    block->next = new_block;
}

/**
 * @brief Coalesce adjacent free blocks
 * 
 * @param arena Arena instance
 */
static void ol_arena_coalesce_free_blocks(ol_arena_t* arena) {
    if (!arena || !arena->free_list) return;
    
    arena_free_node_t* current = arena->free_list;
    while (current && current->next) {
        void* current_end = (char*)current + current->size;
        if (current_end == (void*)current->next) {
            /* Merge current with next */
            current->size += current->next->size;
            current->next = current->next->next;
            arena->header->free_blocks--;
        } else {
            current = current->next;
        }
    }
}

/**
 * @brief Expand arena memory pool without copying data
 * 
 * @param arena Arena to expand
 * @param additional_size Additional size needed
 * @return int OL_SUCCESS on success, OL_ERROR on failure
 */
static int ol_arena_expand_pool(ol_arena_t* arena, size_t additional_size) {
    if (!arena || additional_size == 0) {
        return OL_ERROR;
    }
    
    /* Calculate new total size */
    size_t old_total_size = arena->header->total_size;
    size_t new_total_size = old_total_size + additional_size;
    size_t new_pool_size = new_total_size - sizeof(arena_header_t);
    
    /* Reallocate memory */
#if defined(_WIN32)
    /* On Windows, try to use VirtualAlloc with MEM_COMMIT to expand */
    void* old_memory = arena->header;
    
    /* Try to commit more memory at the end */
    void* additional_mem = VirtualAlloc(
        (char*)old_memory + old_total_size,
        additional_size,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE
    );
    
    if (additional_mem == (char*)old_memory + old_total_size) {
        /* Success! Memory expanded in-place */
        arena->header->total_size = new_total_size;
        arena->pool_size = new_pool_size;
        
        /* Reinitialize guard pages */
        ol_arena_destroy_guards(arena);
        ol_arena_init_guards(arena);
        
        arena->expansion_count++;
        return OL_SUCCESS;
    }
    
    /* Fallback to allocate new region and copy */
    void* new_memory = VirtualAlloc(NULL, new_total_size,
                                   MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE);
    if (!new_memory) {
        return OL_ERROR;
    }
    
    memcpy(new_memory, old_memory, old_total_size);
    VirtualFree(old_memory, 0, MEM_RELEASE);
    
    /* Update arena pointers */
    arena->header = (arena_header_t*)new_memory;
    arena->memory_pool = (char*)new_memory + sizeof(arena_header_t);
    arena->pool_size = new_pool_size;
    arena->header->total_size = new_total_size;
    
#else
    /* Unix-like systems (Linux, macOS, etc.) */
    void* new_memory = MAP_FAILED;
    
    #if defined(__linux__)
        /* Try mremap first on Linux (most efficient) */
        new_memory = mremap(arena->header, old_total_size,
                           new_total_size, MREMAP_MAYMOVE);
    #endif
    
    if (new_memory == MAP_FAILED) {
        /* Fallback for macOS and Linux when mremap fails or not available */
        /* First, reserve the larger address space */
        new_memory = mmap(NULL, new_total_size,
                         PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS,
                         -1, 0);
        if (new_memory == MAP_FAILED) {
            return OL_ERROR;
        }
        
        /* Map the old memory at the start of new region */
        if (mmap(new_memory, old_total_size,
                 PROT_READ | PROT_WRITE,
                 MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
                 -1, 0) == MAP_FAILED) {
            munmap(new_memory, new_total_size);
            return OL_ERROR;
        }
        
        /* Map the additional memory after the old region */
        void* additional_start = (char*)new_memory + old_total_size;
        if (mmap(additional_start, additional_size,
                 PROT_READ | PROT_WRITE,
                 MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
                 -1, 0) == MAP_FAILED) {
            munmap(new_memory, new_total_size);
            return OL_ERROR;
        }
        
        /* Now we can unmap the old region */
        munmap(arena->header, old_total_size);
    }
    
    /* Update arena pointers */
    arena->header = (arena_header_t*)new_memory;
    arena->memory_pool = (char*)new_memory + sizeof(arena_header_t);
    arena->pool_size = new_pool_size;
    arena->header->total_size = new_total_size;
#endif
    
    /* Reinitialize guard pages */
    ol_arena_destroy_guards(arena);
    ol_arena_init_guards(arena);
    
    arena->expansion_count++;
    
    return OL_SUCCESS;
}

/* ==================== Public API Implementation ==================== */

ol_arena_t* ol_arena_create(size_t size, bool is_shared) {
    if (size == 0) {
        size = 4 * 1024 * 1024; /* Default 4MB */
    }
    
    /* Ensure size is multiple of page size */
    size = OL_ALIGN_TO_PAGE(size);
    
    /* Calculate total size including header */
    size_t total_size = sizeof(arena_header_t) + size;
    
    /* Allocate memory */
#if defined(_WIN32)
    void* memory = VirtualAlloc(NULL, total_size,
                               MEM_RESERVE | MEM_COMMIT,
                               PAGE_READWRITE);
    if (!memory) {
        return NULL;
    }
#else
    void* memory = mmap(NULL, total_size,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);
    if (memory == MAP_FAILED) {
        return NULL;
    }
#endif
    
    /* Initialize arena structure */
    ol_arena_t* arena = (ol_arena_t*)calloc(1, sizeof(ol_arena_t));
    if (!arena) {
#if defined(_WIN32)
        VirtualFree(memory, 0, MEM_RELEASE);
#else
        munmap(memory, total_size);
#endif
        return NULL;
    }
    
    /* Initialize header */
    arena->header = (arena_header_t*)memory;
    arena->header->total_size = total_size;
    arena->header->used_size = 0;
    arena->header->allocated_blocks = 0;
    arena->header->free_blocks = 0;
    arena->header->peak_usage = 0;
    arena->header->is_shared = is_shared;
    arena->header->owner_pid = 0; /* Set by process */
    memset(arena->header->guard_pattern, GUARD_PATTERN, 64);
    
    /* Set up memory pool */
    arena->memory_pool = (char*)memory + sizeof(arena_header_t);
    arena->pool_size = size;
    
    /* Initialize free list */
    arena->free_list = NULL;
    arena->free_list_size = 0;
    
    /* Initialize statistics */
    arena->total_allocations = 0;
    arena->total_frees = 0;
    arena->expansion_count = 0;
    
    /* Initialize mutex */
    if (ol_mutex_init(&arena->mutex) != OL_SUCCESS) {
        free(arena);
#if defined(_WIN32)
        VirtualFree(memory, 0, MEM_RELEASE);
#else
        munmap(memory, total_size);
#endif
        return NULL;
    }
    
    /* Initialize guard pages */
    if (ol_arena_init_guards(arena) != OL_SUCCESS) {
        ol_mutex_destroy(&arena->mutex);
        free(arena);
#if defined(_WIN32)
        VirtualFree(memory, 0, MEM_RELEASE);
#else
        munmap(memory, total_size);
#endif
        return NULL;
    }
    
    return arena;
}

void ol_arena_destroy(ol_arena_t* arena) {
    if (!arena) return;
    
    /* Destroy guard pages */
    ol_arena_destroy_guards(arena);
    
    /* Free allocated memory */
    if (arena->header) {
#if defined(_WIN32)
        VirtualFree(arena->header, 0, MEM_RELEASE);
#else
        munmap(arena->header, arena->header->total_size);
#endif
        arena->header = NULL;
    }
    
    /* Destroy mutex */
    ol_mutex_destroy(&arena->mutex);
    
    /* Free arena structure */
    free(arena);
}

void* ol_arena_alloc(ol_arena_t* arena, size_t size) {
    if (!arena || size == 0) {
        return NULL;
    }
    
    ol_mutex_lock(&arena->mutex);
    
    /* Align size to 8 bytes */
    size = (size + 7) & ~7;
    
    /* Add header overhead */
    size_t total_size = sizeof(alloc_header_t) + size + 16; /* +16 for end guard */
    
    /* Check if we need to expand */
    size_t available = arena->pool_size - arena->header->used_size;
    if (available < total_size) {
        if (ol_arena_expand(arena, total_size - available + OL_PAGE_SIZE * 4) != OL_SUCCESS) {
            ol_mutex_unlock(&arena->mutex);
            return NULL;
        }
    }
    
    /* Try free list first */
    arena_free_node_t** free_block_ptr = ol_arena_find_free_block(arena, total_size);
    void* block = NULL;
    
    if (free_block_ptr && *free_block_ptr) {
        /* Use free block */
        arena_free_node_t* free_block = *free_block_ptr;
        block = (void*)free_block;
        
        /* Split if necessary */
        ol_arena_split_free_block(free_block, total_size);
        
        /* Remove from free list */
        *free_block_ptr = free_block->next;
        arena->free_list_size -= free_block->size;
        arena->header->free_blocks--;
    } else {
        /* Bump allocation from pool */
        block = (char*)arena->memory_pool + arena->header->used_size;
        arena->header->used_size += total_size;
    }
    
    /* Initialize allocation header */
    alloc_header_t* header = (alloc_header_t*)block;
    header->size = size;
    header->magic = ALLOC_MAGIC;
    memset(header->guard_start, GUARD_PATTERN, 16);
    
    /* Add end guard */
    uint8_t* end_guard = (uint8_t*)block + sizeof(alloc_header_t) + size;
    memset(end_guard, GUARD_PATTERN, 16);
    
    /* Get user pointer */
    void* user_ptr = (char*)block + sizeof(alloc_header_t);
    
    /* Update statistics */
    arena->header->allocated_blocks++;
    arena->total_allocations++;
    
    if (arena->header->used_size > arena->header->peak_usage) {
        arena->header->peak_usage = arena->header->used_size;
    }
    
    ol_mutex_unlock(&arena->mutex);
    
    return user_ptr;
}

void* ol_arena_alloc_aligned(ol_arena_t* arena, size_t alignment, size_t size) {
    if (!arena || size == 0 || alignment == 0) {
        return NULL;
    }
    
    /* Ensure alignment is power of 2 */
    if ((alignment & (alignment - 1)) != 0) {
        return NULL;
    }
    
    /* Calculate total size with alignment overhead */
    size_t header_size = sizeof(alloc_header_t) + 16; /* +16 for end guard */
    size_t total_size = header_size + size + alignment - 1;
    
    /* Allocate unaligned block */
    void* unaligned_block = ol_arena_alloc(arena, total_size);
    if (!unaligned_block) {
        return NULL;
    }
    
    /* Calculate aligned address */
    uintptr_t unaligned_addr = (uintptr_t)unaligned_block;
    uintptr_t aligned_addr = (unaligned_addr + alignment - 1) & ~(alignment - 1);
    
    /* Check if we need to adjust for header */
    if (aligned_addr - unaligned_addr < header_size) {
        aligned_addr += alignment;
    }
    
    /* Get header pointer */
    alloc_header_t* header = (alloc_header_t*)(aligned_addr - header_size);
    
    /* Store offset for freeing */
    header->size = size | ((aligned_addr - unaligned_addr) << 32);
    
    /* Return aligned user pointer */
    return (void*)aligned_addr;
}

void ol_arena_free(ol_arena_t* arena, void* ptr) {
    if (!arena || !ptr) {
        return;
    }
    
    ol_mutex_lock(&arena->mutex);
    
    /* Get allocation header */
    alloc_header_t* header = (alloc_header_t*)((char*)ptr - sizeof(alloc_header_t));
    
    /* Validate allocation */
    if (ol_arena_validate_allocation(header) != OL_SUCCESS) {
        ol_mutex_unlock(&arena->mutex);
        return; /* Corrupted or invalid allocation */
    }
    
    /* Extract size */
    size_t size = header->size & 0xFFFFFFFF;
    size_t offset = header->size >> 32;
    
    /* Calculate actual block start */
    void* block_start = (char*)header - offset;
    size_t block_size = sizeof(alloc_header_t) + size + 16;
    if (offset > 0) {
        block_size += offset;
    }
    
    /* Clear magic to prevent reuse */
    header->magic = 0;
    
    /* Add to free list */
    arena_free_node_t* free_node = (arena_free_node_t*)block_start;
    free_node->size = block_size;
    free_node->next = arena->free_list;
    arena->free_list = free_node;
    
    /* Update statistics */
    arena->header->allocated_blocks--;
    arena->header->free_blocks++;
    arena->free_list_size += block_size;
    arena->total_frees++;
    
    /* Coalesce free blocks periodically */
    if (arena->header->free_blocks > 16) {
        ol_arena_coalesce_free_blocks(arena);
    }
    
    ol_mutex_unlock(&arena->mutex);
}

void ol_arena_reset(ol_arena_t* arena) {
    if (!arena) return;
    
    ol_mutex_lock(&arena->mutex);
    
    /* Reset memory pool */
    arena->header->used_size = 0;
    arena->header->allocated_blocks = 0;
    arena->header->free_blocks = 0;
    arena->header->peak_usage = 0;
    
    /* Clear free list */
    arena->free_list = NULL;
    arena->free_list_size = 0;
    
    /* Reset statistics */
    arena->total_allocations = 0;
    arena->total_frees = 0;
    
    ol_mutex_unlock(&arena->mutex);
}

int ol_arena_get_stats(const ol_arena_t* arena, ol_arena_stats_t* stats) {
    if (!arena || !stats) {
        return OL_ERROR;
    }
    
    ol_mutex_lock((ol_mutex_t*)&arena->mutex);
    
    stats->total_size = arena->pool_size;
    stats->used_size = arena->header->used_size;
    stats->alloc_count = arena->header->allocated_blocks;
    stats->free_count = arena->header->free_blocks;
    stats->peak_usage = arena->header->peak_usage;
    
    ol_mutex_unlock((ol_mutex_t*)&arena->mutex);
    
    return OL_SUCCESS;
}

bool ol_arena_contains(const ol_arena_t* arena, const void* ptr) {
    if (!arena || !ptr) {
        return false;
    }
    
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t pool_start = (uintptr_t)arena->memory_pool;
    uintptr_t pool_end = pool_start + arena->pool_size;
    
    return (addr >= pool_start && addr < pool_end);
}

size_t ol_arena_total_size(const ol_arena_t* arena) {
    if (!arena) return 0;
    return arena->pool_size;
}

size_t ol_arena_used_size(const ol_arena_t* arena) {
    if (!arena) return 0;
    
    size_t used;
    ol_mutex_lock((ol_mutex_t*)&arena->mutex);
    used = arena->header->used_size;
    ol_mutex_unlock((ol_mutex_t*)&arena->mutex);
    
    return used;
}

int ol_arena_expand(ol_arena_t* arena, size_t additional_size) {
    if (!arena || additional_size == 0) {
        return OL_ERROR;
    }
    
    ol_mutex_lock(&arena->mutex);
    
    /* Align to page size */
    additional_size = OL_ALIGN_TO_PAGE(additional_size);
    
    int result = ol_arena_expand_pool(arena, additional_size);
    
    ol_mutex_unlock(&arena->mutex);
    
    return result;
}

ol_arena_t* ol_arena_create_sub(ol_arena_t* parent, size_t size) {
    if (!parent) {
        return NULL;
    }
    
    /* Allocate from parent arena */
    void* sub_pool = ol_arena_alloc(parent, sizeof(ol_arena_t) + size);
    if (!sub_pool) {
        return NULL;
    }
    
    /* Initialize sub-arena structure */
    ol_arena_t* sub_arena = (ol_arena_t*)sub_pool;
    sub_arena->header = NULL; /* Sub-arena shares parent's header */
    sub_arena->memory_pool = (char*)sub_pool + sizeof(ol_arena_t);
    sub_arena->pool_size = size;
    sub_arena->free_list = NULL;
    sub_arena->free_list_size = 0;
    sub_arena->guard_before = NULL;
    sub_arena->guard_after = NULL;
    sub_arena->guard_size = 0;
    
    /* Use parent's mutex for synchronization */
    sub_arena->mutex = parent->mutex;
    
    /* Initialize statistics */
    sub_arena->total_allocations = 0;
    sub_arena->total_frees = 0;
    sub_arena->expansion_count = 0;
    
    return sub_arena;
}

bool ol_arena_is_shared(const ol_arena_t* arena) {
    if (!arena || !arena->header) return false;
    return arena->header->is_shared;
}
