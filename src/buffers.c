/* OLSRT - Buffers & Arena */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>
#include <string.h>

/* ================================ Buffers =================================== */

ol_buf_t* ol_buf_alloc(size_t cap) {
    ol_buf_t *b = (ol_buf_t*)calloc(1, sizeof(*b));
    if (!b) return NULL;
    if (cap == 0) cap = 1;
    b->data = (uint8_t*)malloc(cap);
    if (!b->data) { free(b); return NULL; }
    b->cap = cap;
    b->len = 0;
    return b;
}

int ol_buf_reserve(ol_buf_t *b, size_t cap) {
    if (!b) return OL_ERR_STATE;
    if (cap <= b->cap) return OL_OK;
    uint8_t *p = (uint8_t*)realloc(b->data, cap);
    if (!p) return OL_ERR_ALLOC;
    b->data = p;
    b->cap  = cap;
    return OL_OK;
}

int ol_buf_append(ol_buf_t *b, const void *src, size_t n) {
    if (!b || (!src && n)) return OL_ERR_STATE;
    if (b->len + n > b->cap) {
        size_t need = b->len + n;
        /* Growth policy: double until >= need */
        size_t new_cap = b->cap ? b->cap : 1;
        while (new_cap < need) new_cap *= 2;
        if (ol_buf_reserve(b, new_cap) != OL_OK) return OL_ERR_ALLOC;
    }
    if (n) memcpy(b->data + b->len, src, n);
    b->len += n;
    return OL_OK;
}

int ol_buf_slice(ol_buf_t *b, size_t off, size_t n, ol_buf_t **out) {
    if (!b || !out) return OL_ERR_STATE;
    if (off > b->len || off + n > b->len) return OL_ERR_RANGE;
    ol_buf_t *s = ol_buf_alloc(n);
    if (!s) return OL_ERR_ALLOC;
    if (n) memcpy(s->data, b->data + off, n);
    s->len = n;
    *out = s;
    return OL_OK;
}

void ol_buf_clear(ol_buf_t *b) { if (b) b->len = 0; }

void ol_buf_free(ol_buf_t *b) {
    if (!b) return;
    free(b->data);
    free(b);
}

/* ================================= Arena ==================================== */

typedef struct ol_block_s {
    struct ol_block_s *next;
    size_t used;
    size_t cap;
    uint8_t data[]; /* flexible array member */
} ol_block_t;

struct ol_arena_s {
    size_t    bsize; /* default block size */
    ol_block_t *head;
};

static ol_block_t* ol_block_new(size_t cap) {
    ol_block_t *blk = (ol_block_t*)malloc(sizeof(ol_block_t) + cap);
    if (!blk) return NULL;
    blk->next = NULL;
    blk->used = 0;
    blk->cap  = cap;
    return blk;
}

ol_arena_t* ol_arena_create(size_t block_size) {
    if (block_size < 4096) block_size = 4096;
    ol_arena_t *a = (ol_arena_t*)calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->bsize = block_size;
    a->head  = NULL;
    return a;
}

void* ol_arena_alloc(ol_arena_t *a, size_t n) {
    if (!a) return NULL;
    if (n == 0) n = 1;
    if (!a->head || a->head->used + n > a->head->cap) {
        size_t cap = (n > a->bsize) ? n : a->bsize;
        ol_block_t *blk = ol_block_new(cap);
        if (!blk) return NULL;
        blk->next = a->head;
        a->head   = blk;
    }
    void *p = a->head->data + a->head->used;
    a->head->used += n;
    return p;
}

void ol_arena_reset(ol_arena_t *a) {
    if (!a) return;
    ol_block_t *b = a->head;
    while (b) {
        ol_block_t *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}

void ol_arena_destroy(ol_arena_t *a) {
    if (!a) return;
    ol_arena_reset(a);
    free(a);
}
