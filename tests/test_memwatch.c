/**
 * @file test_memwatch.c
 * @brief Memory watcher tests.
 *
 * Covers:
 *   - init / shutdown lifecycle
 *   - usage tracking through malloc / free
 *   - peak usage
 *   - realloc accounting (this is the test the CHANGELOG promised;
 *     the fixed ol_memwatch_realloc detaches the old record, calls
 *     realloc, then registers the new pointer, and the accounting
 *     must reflect exactly the new size)
 */

#include "ol_memwatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

static void expect(int cond, const char *msg, const char *file, int line)
{
    if (cond) {
        printf("  [PASS] %s\n", msg);
        g_pass++;
    } else {
        printf("  [FAIL] %s  (at %s:%d)\n", msg, file, line);
        g_fail++;
    }
}

#define EXPECT(cond, msg) expect((cond), (msg), __FILE__, __LINE__)

int main(void)
{
    printf("Memory watcher test suite\n");
    printf("=========================\n");

    printf("\nTest 1: init\n");
    int r = ol_memwatch_init();
    EXPECT(r == 0, "ol_memwatch_init returned 0");
    EXPECT(ol_memwatch_get_usage() == 0, "usage is 0 after init");

    printf("\nTest 2: single allocation\n");
    void *p = ol_memwatch_malloc(100);
    EXPECT(p != NULL, "ol_memwatch_malloc returned a pointer");
    EXPECT(ol_memwatch_get_usage() == 100, "usage == 100");
    EXPECT(ol_memwatch_get_peak() >= 100, "peak >= 100");

    printf("\nTest 3: second allocation\n");
    void *q = ol_memwatch_calloc(4, 50);   /* 200 bytes, zeroed */
    EXPECT(q != NULL, "ol_memwatch_calloc returned a pointer");
    EXPECT(ol_memwatch_get_usage() == 300, "usage == 300");

    /* calloc must zero the buffer */
    const unsigned char *qq = (const unsigned char *)q;
    int zeroed = 1;
    for (int i = 0; i < 200; i++) {
        if (qq[i] != 0) { zeroed = 0; break; }
    }
    EXPECT(zeroed, "calloc zeroed the buffer");

    printf("\nTest 4: realloc grows\n");
    void *q2 = ol_memwatch_realloc(q, 500);
    EXPECT(q2 != NULL, "realloc returned a pointer");
    EXPECT(ol_memwatch_get_usage() == 600,
           "usage after realloc == 100 + 500 (not 100 + 200 + 500)");

    /* The first 200 bytes must still hold their old values (zeros). */
    const unsigned char *q2b = (const unsigned char *)q2;
    int preserved = 1;
    for (int i = 0; i < 200; i++) {
        if (q2b[i] != 0) { preserved = 0; break; }
    }
    EXPECT(preserved, "realloc preserved the old contents");

    printf("\nTest 5: realloc shrinks\n");
    void *q3 = ol_memwatch_realloc(q2, 50);
    EXPECT(q3 != NULL, "shrinking realloc returned a pointer");
    EXPECT(ol_memwatch_get_usage() == 150,
           "usage after shrink == 100 + 50");

    printf("\nTest 6: free path\n");
    ol_memwatch_free(p);
    EXPECT(ol_memwatch_get_usage() == 50, "usage == 50 after first free");

    ol_memwatch_free(q3);
    EXPECT(ol_memwatch_get_usage() == 0, "usage == 0 after all freed");

    printf("\nTest 7: peak is sticky\n");
    EXPECT(ol_memwatch_get_peak() >= 600,
           "peak still reflects the 600-byte high-water mark");

    /* Shutdown would normally print a leak report to stderr; since
     * we have no leaks, it should print nothing. */
    ol_memwatch_shutdown();

    printf("\n=========================\n");
    printf("Passed: %d\n", g_pass);
    printf("Failed: %d\n", g_fail);

    return g_fail == 0 ? 0 : 1;
}
