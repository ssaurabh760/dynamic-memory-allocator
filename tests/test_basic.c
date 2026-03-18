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

static void test_init(void) {
    mem_init();
    int result = mm_init();
    if (result == 0)
        PASS("mm_init returns 0");
    else
        FAIL("mm_init returns 0");
    mem_deinit();
}

static void test_single_malloc(void) {
    mem_init();
    mm_init();

    void *p = mm_malloc(16);
    if (p != NULL)
        PASS("single malloc returns non-NULL");
    else
        FAIL("single malloc returns non-NULL");

    /* Write to the allocated memory to verify it's usable */
    memset(p, 0xAB, 16);
    unsigned char *cp = (unsigned char *)p;
    int ok = 1;
    for (int i = 0; i < 16; i++) {
        if (cp[i] != 0xAB) {
            ok = 0;
            break;
        }
    }
    if (ok)
        PASS("allocated memory is writable and readable");
    else
        FAIL("allocated memory is writable and readable");

    mem_deinit();
}

static void test_multiple_malloc(void) {
    mem_init();
    mm_init();

    void *p1 = mm_malloc(32);
    void *p2 = mm_malloc(64);
    void *p3 = mm_malloc(128);

    if (p1 != NULL && p2 != NULL && p3 != NULL)
        PASS("multiple mallocs return non-NULL");
    else
        FAIL("multiple mallocs return non-NULL");

    /* Verify no overlap */
    if (p1 != p2 && p2 != p3 && p1 != p3)
        PASS("multiple mallocs return distinct pointers");
    else
        FAIL("multiple mallocs return distinct pointers");

    /* Write distinct patterns and verify */
    memset(p1, 0x11, 32);
    memset(p2, 0x22, 64);
    memset(p3, 0x33, 128);

    unsigned char *c1 = (unsigned char *)p1;
    unsigned char *c2 = (unsigned char *)p2;
    unsigned char *c3 = (unsigned char *)p3;
    int ok = (c1[0] == 0x11 && c1[31] == 0x11 && c2[0] == 0x22 && c2[63] == 0x22 &&
              c3[0] == 0x33 && c3[127] == 0x33);
    if (ok)
        PASS("blocks do not overlap after writes");
    else
        FAIL("blocks do not overlap after writes");

    mem_deinit();
}

static void test_malloc_free_malloc(void) {
    mem_init();
    mm_init();

    void *p1 = mm_malloc(100);
    memset(p1, 0xAA, 100);
    mm_free(p1);

    void *p2 = mm_malloc(100);
    if (p2 != NULL)
        PASS("malloc after free returns non-NULL");
    else
        FAIL("malloc after free returns non-NULL");

    /* Should reuse the freed block */
    memset(p2, 0xBB, 100);
    unsigned char *cp = (unsigned char *)p2;
    if (cp[0] == 0xBB && cp[99] == 0xBB)
        PASS("reused block is writable");
    else
        FAIL("reused block is writable");

    mem_deinit();
}

static void test_alignment(void) {
    mem_init();
    mm_init();

    void *p1 = mm_malloc(1);
    void *p2 = mm_malloc(7);
    void *p3 = mm_malloc(13);

    int aligned = ((unsigned long)p1 % 16 == 0) && ((unsigned long)p2 % 16 == 0) &&
                  ((unsigned long)p3 % 16 == 0);

    if (aligned)
        PASS("all allocations are 16-byte aligned");
    else
        FAIL("all allocations are 16-byte aligned");

    mem_deinit();
}

static void test_zero_size(void) {
    mem_init();
    mm_init();

    void *p = mm_malloc(0);
    if (p == NULL)
        PASS("malloc(0) returns NULL");
    else
        FAIL("malloc(0) returns NULL");

    mem_deinit();
}

static void test_free_null(void) {
    mem_init();
    mm_init();

    /* Should not crash */
    mm_free(NULL);
    PASS("free(NULL) does not crash");

    mem_deinit();
}

int main(void) {
    printf("=== Basic Allocator Tests ===\n");

    test_init();
    test_single_malloc();
    test_multiple_malloc();
    test_malloc_free_malloc();
    test_alignment();
    test_zero_size();
    test_free_null();

    printf("\nResults: %d failure(s)\n", failures);
    return failures > 0 ? 1 : 0;
}
