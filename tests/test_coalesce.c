#include "memlib.h"
#include "mm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PASS(name) printf("  PASS: %s\n", name)
#define FAIL(name)                                                                                 \
    do {                                                                                           \
        printf("  FAIL: %s (line %d)\n", name, __LINE__);                                         \
        failures++;                                                                                \
    } while (0)

static int failures = 0;

/* Test coalescing with next block */
static void test_coalesce_next(void) {
    mem_init();
    mm_init();

    void *p1 = mm_malloc(32);
    void *p2 = mm_malloc(32);
    void *p3 = mm_malloc(32);
    (void)p3; /* keep p3 allocated */

    mm_free(p2); /* free middle block */
    mm_free(p1); /* free first block — should coalesce with p2 */

    /* Allocate a block that requires the combined space of p1+p2 */
    void *p4 = mm_malloc(80);
    if (p4 != NULL)
        PASS("coalesce with next block allows larger allocation");
    else
        FAIL("coalesce with next block allows larger allocation");

    memset(p4, 0xCC, 80);
    mem_deinit();
}

/* Test coalescing with previous block */
static void test_coalesce_prev(void) {
    mem_init();
    mm_init();

    void *p1 = mm_malloc(32);
    void *p2 = mm_malloc(32);
    void *p3 = mm_malloc(32);
    (void)p3;

    mm_free(p1); /* free first block */
    mm_free(p2); /* free second block — should coalesce with p1 */

    /* Allocate a block that requires the combined space */
    void *p4 = mm_malloc(80);
    if (p4 != NULL)
        PASS("coalesce with previous block allows larger allocation");
    else
        FAIL("coalesce with previous block allows larger allocation");

    memset(p4, 0xDD, 80);
    mem_deinit();
}

/* Test coalescing with both neighbors */
static void test_coalesce_both(void) {
    mem_init();
    mm_init();

    void *p1 = mm_malloc(32);
    void *p2 = mm_malloc(32);
    void *p3 = mm_malloc(32);
    void *p4 = mm_malloc(32); /* barrier */
    (void)p4;

    mm_free(p1); /* free first */
    mm_free(p3); /* free third */
    mm_free(p2); /* free middle — should coalesce all three */

    /* Allocate a block that requires the combined space of p1+p2+p3 */
    void *p5 = mm_malloc(112);
    if (p5 != NULL)
        PASS("coalesce with both neighbors allows larger allocation");
    else
        FAIL("coalesce with both neighbors allows larger allocation");

    memset(p5, 0xEE, 112);
    mem_deinit();
}

/* Test no coalescing when neighbors are allocated */
static void test_no_coalesce(void) {
    mem_init();
    mm_init();

    void *p1 = mm_malloc(32);
    void *p2 = mm_malloc(32);
    void *p3 = mm_malloc(32);
    (void)p1;
    (void)p3;

    mm_free(p2); /* free middle — no coalescing since neighbors are allocated */

    /* Verify we can still allocate the same size */
    void *p4 = mm_malloc(32);
    if (p4 != NULL)
        PASS("no coalescing when neighbors allocated");
    else
        FAIL("no coalescing when neighbors allocated");

    mem_deinit();
}

/* Test repeated free and coalesce cycles */
static void test_repeated_coalesce(void) {
    mem_init();
    mm_init();

    for (int i = 0; i < 10; i++) {
        void *p1 = mm_malloc(64);
        void *p2 = mm_malloc(64);
        void *p3 = mm_malloc(64);
        mm_free(p1);
        mm_free(p2);
        mm_free(p3);
    }

    /* After all free/coalesce cycles, should be able to allocate large block */
    void *p = mm_malloc(4000);
    if (p != NULL)
        PASS("repeated coalesce cycles maintain heap integrity");
    else
        FAIL("repeated coalesce cycles maintain heap integrity");

    memset(p, 0xFF, 4000);
    mem_deinit();
}

int main(void) {
    printf("=== Coalescing Tests ===\n");

    test_coalesce_next();
    test_coalesce_prev();
    test_coalesce_both();
    test_no_coalesce();
    test_repeated_coalesce();

    printf("\nResults: %d failure(s)\n", failures);
    return failures > 0 ? 1 : 0;
}
