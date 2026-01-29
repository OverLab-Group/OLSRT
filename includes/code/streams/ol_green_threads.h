#ifndef OL_GREEN_THREADS_H
#define OL_GREEN_THREADS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef void (*ol_gt_entry_fn)(void *arg);

/* Context structure for assembly implementation */
#if defined(__x86_64__)
typedef struct {
    void *rbx, *rbp, *r12, *r13, *r14, *r15, *rsp, *rip;
} ol_gt_ctx_t;
#elif defined(__i386__)
typedef struct {
    void *ebx, *ebp, *esi, *edi, *esp, *eip;
} ol_gt_ctx_t;
#elif defined(__aarch64__)
typedef struct {
    void *x19, *x20, *x21, *x22, *x23, *x24, *x25, *x26;
    void *x27, *x28, *x29, *sp, *x30, *pc;
} ol_gt_ctx_t;
#elif defined(__arm__)
typedef struct {
    void *r4, *r5, *r6, *r7, *r8, *r9, *r10, *r11;
    void *sp, *lr, *pc;
} ol_gt_ctx_t;
#else
/* Generic fallback */
typedef struct {
    char data[128]; /* Enough for most contexts */
} ol_gt_ctx_t;
#endif

/* Opaque green thread handle */
typedef struct ol_gt ol_gt_t;

/* API Functions */
int ol_gt_scheduler_init(void);
void ol_gt_scheduler_shutdown(void);

ol_gt_t* ol_gt_spawn(ol_gt_entry_fn entry, void *arg, size_t stack_size);
int ol_gt_resume(ol_gt_t *gt);
void ol_gt_yield(void);
int ol_gt_join(ol_gt_t *gt);
int ol_gt_cancel(ol_gt_t *gt);
void ol_gt_destroy(ol_gt_t *gt);

bool ol_gt_is_alive(const ol_gt_t *gt);
bool ol_gt_is_canceled(const ol_gt_t *gt);
ol_gt_t* ol_gt_current(void);

#endif /* OL_GREEN_THREADS_H */