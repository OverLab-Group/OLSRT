#ifndef OL_SEMAPHORES_H
#define OL_SEMAPHORES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * v1.3.2: ol_sem_t is a complete type.
 *
 * The previous header declared it as an incomplete typedef while
 * ol_sem_init asked the caller to provide storage. Callers had no
 * way to know the struct size, so the type was unusable on the
 * stack. The struct is now fully defined here; the implementation
 * no longer redeclares it.
 */

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
typedef struct {
    HANDLE       h;         /* Win32 semaphore handle */
    unsigned int max_count; /* advisory upper bound */
} ol_sem_t;
#else
#include <semaphore.h>
typedef struct {
    sem_t        sem;       /* POSIX counting semaphore */
    unsigned int max_count; /* advisory upper bound */
} ol_sem_t;
#endif

int ol_sem_init(ol_sem_t* s, unsigned int initial, unsigned int max_count);
int ol_sem_destroy(ol_sem_t* s);
int ol_sem_post(ol_sem_t* s);
int ol_sem_trywait(ol_sem_t* s);
int ol_sem_wait_until(ol_sem_t* s, int64_t deadline_ns);
int ol_sem_getvalue(ol_sem_t* s, int* out_value);

#ifdef __cplusplus
}
#endif

#endif /* OL_SEMAPHORES_H */
