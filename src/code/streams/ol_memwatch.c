/**
 * @file ol_memwatch.c
 * @brief Memory leak detection implementation
 */

#include "ol_memwatch.h"
#include "ol_lock_mutex.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <execinfo.h>

/* Configuration */
#define MEMWATCH_HASH_SIZE 4096
#define MAX_BACKTRACE_DEPTH 16

/* Allocation record */
typedef struct mem_record {
    void *ptr;
    size_t size;
    const char *file;
    int line;
    void *backtrace[MAX_BACKTRACE_DEPTH];
    int bt_depth;
    struct mem_record *next;
    uint64_t timestamp;
} mem_record_t;

/* Memory watcher state */
static struct {
    bool initialized;
    bool enabled;
    size_t threshold;
    size_t current_usage;
    size_t peak_usage;
    size_t total_allocations;
    size_t total_frees;

    /* Hash table for quick lookup */
    mem_record_t *hash_table[MEMWATCH_HASH_SIZE];

    /* Synchronization */
    ol_mutex_t mutex;

    /* Callback for custom logging */
    void (*log_callback)(const char *msg, void *userdata);
    void *log_userdata;
} g_memwatch = {0};

/* Hash function for pointers */
static inline size_t ptr_hash(void *ptr) {
    uintptr_t p = (uintptr_t)ptr;
    return (p ^ (p >> 16)) % MEMWATCH_HASH_SIZE;
}

/* Get backtrace if available */
static int get_backtrace(void **buffer, int max_depth) {
    #if defined(__linux__) || defined(__APPLE__)
    return backtrace(buffer, max_depth);
    #else
    (void)buffer;
    (void)max_depth;
    return 0;
    #endif
}

/* Symbolicate backtrace */
static void symbolize_backtrace(void **buffer, int depth, char *out, size_t out_size) {
    #if defined(__linux__) || defined(__APPLE__)
    char **symbols = backtrace_symbols(buffer, depth);
    if (symbols) {
        out[0] = '\0';
        for (int i = 0; i < depth; i++) {
            strncat(out, symbols[i], out_size - strlen(out) - 1);
            strncat(out, "\n", out_size - strlen(out) - 1);
        }
        free(symbols);
    }
    #else
    (void)buffer;
    (void)depth;
    (void)out;
    (void)out_size;
    #endif
}

/* Initialize memory watcher */
int ol_memwatch_init(void) {
    if (g_memwatch.initialized) return 0;

    if (ol_mutex_init(&g_memwatch.mutex) != 0) {
        return -1;
    }

    memset(g_memwatch.hash_table, 0, sizeof(g_memwatch.hash_table));
    g_memwatch.enabled = true;
    g_memwatch.threshold = 1024; /* 1KB default threshold */
    g_memwatch.initialized = true;

    return 0;
}

/* Shutdown and report leaks */
void ol_memwatch_shutdown(void) {
    if (!g_memwatch.initialized) return;

    ol_mutex_lock(&g_memwatch.mutex);

    if (g_memwatch.current_usage > 0) {
        fprintf(stderr, "\n=== MEMORY LEAK REPORT ===\n");
        fprintf(stderr, "Total leaks: %zu bytes in %zu allocations\n",
                g_memwatch.current_usage, g_memwatch.total_allocations - g_memwatch.total_frees);

        /* Dump all remaining allocations */
        for (size_t i = 0; i < MEMWATCH_HASH_SIZE; i++) {
            mem_record_t *rec = g_memwatch.hash_table[i];
            while (rec) {
                if (rec->size >= g_memwatch.threshold) {
                    fprintf(stderr, "Leak: %p (%zu bytes)", rec->ptr, rec->size);
                    if (rec->file) {
                        fprintf(stderr, " at %s:%d", rec->file, rec->line);
                    }
                    fprintf(stderr, "\n");

                    if (rec->bt_depth > 0) {
                        char bt_buffer[4096];
                        symbolize_backtrace(rec->backtrace, rec->bt_depth,
                                            bt_buffer, sizeof(bt_buffer));
                        fprintf(stderr, "Backtrace:\n%s", bt_buffer);
                    }
                }
                rec = rec->next;
            }
        }

        fprintf(stderr, "Peak memory usage: %zu bytes\n", g_memwatch.peak_usage);
        fprintf(stderr, "Total allocations: %zu, Total frees: %zu\n",
                g_memwatch.total_allocations, g_memwatch.total_frees);
        fprintf(stderr, "===========================\n");
    }

    /* Free all records */
    for (size_t i = 0; i < MEMWATCH_HASH_SIZE; i++) {
        mem_record_t *rec = g_memwatch.hash_table[i];
        while (rec) {
            mem_record_t *next = rec->next;
            free(rec);
            rec = next;
        }
        g_memwatch.hash_table[i] = NULL;
    }

    g_memwatch.initialized = false;
    ol_mutex_unlock(&g_memwatch.mutex);

    ol_mutex_destroy(&g_memwatch.mutex);
}

/* Track allocation */
void *ol_memwatch_track_alloc(size_t size, const char *file, int line) {
    if (!g_memwatch.initialized || !g_memwatch.enabled) {
        return malloc(size);
    }

    void *ptr = malloc(size);
    if (!ptr) return NULL;

    ol_mutex_lock(&g_memwatch.mutex);

    /* Create record */
    mem_record_t *rec = (mem_record_t*)malloc(sizeof(mem_record_t));
    if (rec) {
        rec->ptr = ptr;
        rec->size = size;
        rec->file = file;
        rec->line = line;
        rec->bt_depth = get_backtrace(rec->backtrace, MAX_BACKTRACE_DEPTH);
        rec->timestamp = 0; /* Could use clock_gettime here */

        /* Add to hash table */
        size_t hash = ptr_hash(ptr);
        rec->next = g_memwatch.hash_table[hash];
        g_memwatch.hash_table[hash] = rec;

        /* Update statistics */
        g_memwatch.current_usage += size;
        g_memwatch.total_allocations++;

        if (g_memwatch.current_usage > g_memwatch.peak_usage) {
            g_memwatch.peak_usage = g_memwatch.current_usage;
        }
    }

    ol_mutex_unlock(&g_memwatch.mutex);

    return ptr;
}

/* Track free */
void ol_memwatch_track_free(void *ptr, const char *file, int line) {
    if (!ptr) return;

    if (!g_memwatch.initialized || !g_memwatch.enabled) {
        free(ptr);
        return;
    }

    ol_mutex_lock(&g_memwatch.mutex);

    size_t hash = ptr_hash(ptr);
    mem_record_t **prev = &g_memwatch.hash_table[hash];
    mem_record_t *rec = *prev;

    while (rec) {
        if (rec->ptr == ptr) {
            /* Remove from hash table */
            *prev = rec->next;

            /* Update statistics */
            g_memwatch.current_usage -= rec->size;
            g_memwatch.total_frees++;

            free(rec);
            break;
        }
        prev = &rec->next;
        rec = rec->next;
    }

    ol_mutex_unlock(&g_memwatch.mutex);

    free(ptr);
}

/* Wrapped allocation functions */
void *ol_memwatch_malloc(size_t size) {
    return ol_memwatch_track_alloc(size, NULL, 0);
}

void *ol_memwatch_calloc(size_t nmemb, size_t size) {
    void *ptr = ol_memwatch_track_alloc(nmemb * size, NULL, 0);
    if (ptr) {
        memset(ptr, 0, nmemb * size);
    }
    return ptr;
}

void *ol_memwatch_realloc(void *ptr, size_t size) {
    if (!ptr) return ol_memwatch_track_alloc(size, NULL, 0);

    /* Find old size */
    size_t old_size = 0;
    if (g_memwatch.initialized && g_memwatch.enabled) {
        ol_mutex_lock(&g_memwatch.mutex);
        size_t hash = ptr_hash(ptr);
        mem_record_t *rec = g_memwatch.hash_table[hash];
        while (rec) {
            if (rec->ptr == ptr) {
                old_size = rec->size;
                break;
            }
            rec = rec->next;
        }
        ol_mutex_unlock(&g_memwatch.mutex);
    }

    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) return NULL;

    if (g_memwatch.initialized && g_memwatch.enabled && new_ptr != ptr) {
        /* Remove old record */
        ol_memwatch_track_free(ptr, NULL, 0);

        /* Add new record */
        ol_memwatch_track_alloc(size, NULL, 0);
    }

    return new_ptr;
}

void ol_memwatch_free(void *ptr) {
    ol_memwatch_track_free(ptr, NULL, 0);
}

/* API functions */
void ol_memwatch_enable(bool enabled) {
    if (!g_memwatch.initialized) return;
    ol_mutex_lock(&g_memwatch.mutex);
    g_memwatch.enabled = enabled;
    ol_mutex_unlock(&g_memwatch.mutex);
}

size_t ol_memwatch_get_usage(void) {
    if (!g_memwatch.initialized) return 0;
    size_t usage;
    ol_mutex_lock(&g_memwatch.mutex);
    usage = g_memwatch.current_usage;
    ol_mutex_unlock(&g_memwatch.mutex);
    return usage;
}

size_t ol_memwatch_get_peak(void) {
    if (!g_memwatch.initialized) return 0;
    size_t peak;
    ol_mutex_lock(&g_memwatch.mutex);
    peak = g_memwatch.peak_usage;
    ol_mutex_unlock(&g_memwatch.mutex);
    return peak;
}

void ol_memwatch_dump(void) {
    if (!g_memwatch.initialized) return;

    ol_mutex_lock(&g_memwatch.mutex);

    fprintf(stderr, "Memory Watcher Status:\n");
    fprintf(stderr, "  Current usage: %zu bytes\n", g_memwatch.current_usage);
    fprintf(stderr, "  Peak usage:    %zu bytes\n", g_memwatch.peak_usage);
    fprintf(stderr, "  Allocations:   %zu\n", g_memwatch.total_allocations);
    fprintf(stderr, "  Frees:         %zu\n", g_memwatch.total_frees);
    fprintf(stderr, "  Leaks:         %zu bytes\n",
            g_memwatch.current_usage);

    ol_mutex_unlock(&g_memwatch.mutex);
}

void ol_memwatch_set_threshold(size_t threshold) {
    if (!g_memwatch.initialized) return;
    ol_mutex_lock(&g_memwatch.mutex);
    g_memwatch.threshold = threshold;
    ol_mutex_unlock(&g_memwatch.mutex);
}
