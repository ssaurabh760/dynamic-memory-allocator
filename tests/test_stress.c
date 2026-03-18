#include "memlib.h"
#include "mm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PASS(name) printf("  PASS: %s\n", name)
#define FAIL(name)                                                                                 \
    do {                                                                                           \
        printf("  FAIL: %s (line %d)\n", name, __LINE__);                                         \
        failures++;                                                                                \
    } while (0)

static int failures = 0;

/* Test many small allocations */
static void test_many_small(void) {
    mem_init();
    mm_init();

    int n = 1000;
    void *ptrs[1000];
    int ok = 1;

    for (int i = 0; i < n; i++) {
        ptrs[i] = mm_malloc(16);
        if (ptrs[i] == NULL) {
            ok = 0;
            break;
        }
        memset(ptrs[i], (unsigned char)(i & 0xFF), 16);
    }

    if (ok)
        PASS("1000 small allocations succeed");
    else
        FAIL("1000 small allocations succeed");

    /* Free all */
    for (int i = 0; i < n; i++) {
        if (ptrs[i] != NULL)
            mm_free(ptrs[i]);
    }

    /* Should be able to allocate a large block after freeing all */
    void *p = mm_malloc(8000);
    if (p != NULL)
        PASS("large allocation after freeing 1000 small blocks");
    else
        FAIL("large allocation after freeing 1000 small blocks");

    mem_deinit();
}

/* Test alternating malloc/free pattern */
static void test_alternating(void) {
    mem_init();
    mm_init();

    int n = 500;
    void *ptrs[500];
    int ok = 1;

    /* Allocate all */
    for (int i = 0; i < n; i++) {
        ptrs[i] = mm_malloc(32 + (i % 128));
        if (ptrs[i] == NULL) {
            ok = 0;
            break;
        }
    }

    /* Free every other */
    for (int i = 0; i < n; i += 2) {
        mm_free(ptrs[i]);
        ptrs[i] = NULL;
    }

    /* Reallocate in the gaps */
    for (int i = 0; i < n; i += 2) {
        ptrs[i] = mm_malloc(32 + (i % 128));
        if (ptrs[i] == NULL) {
            ok = 0;
            break;
        }
    }

    if (ok)
        PASS("alternating malloc/free pattern");
    else
        FAIL("alternating malloc/free pattern");

    /* Cleanup */
    for (int i = 0; i < n; i++) {
        if (ptrs[i] != NULL)
            mm_free(ptrs[i]);
    }

    mem_deinit();
}

/* Test realloc correctness */
static void test_realloc(void) {
    mem_init();
    mm_init();

    /* Grow */
    void *p = mm_malloc(16);
    memset(p, 0xAA, 16);

    p = mm_realloc(p, 64);
    if (p != NULL) {
        unsigned char *cp = (unsigned char *)p;
        int ok = 1;
        for (int i = 0; i < 16; i++) {
            if (cp[i] != 0xAA) {
                ok = 0;
                break;
            }
        }
        if (ok)
            PASS("realloc preserves data when growing");
        else
            FAIL("realloc preserves data when growing");
    } else {
        FAIL("realloc returns non-NULL when growing");
    }

    /* Shrink */
    p = mm_realloc(p, 8);
    if (p != NULL) {
        unsigned char *cp = (unsigned char *)p;
        if (cp[0] == 0xAA)
            PASS("realloc preserves data when shrinking");
        else
            FAIL("realloc preserves data when shrinking");
    } else {
        FAIL("realloc returns non-NULL when shrinking");
    }

    /* realloc(NULL, size) == malloc(size) */
    void *p2 = mm_realloc(NULL, 32);
    if (p2 != NULL)
        PASS("realloc(NULL, size) works like malloc");
    else
        FAIL("realloc(NULL, size) works like malloc");

    /* realloc(ptr, 0) == free(ptr) */
    void *p3 = mm_realloc(p, 0);
    if (p3 == NULL)
        PASS("realloc(ptr, 0) works like free");
    else
        FAIL("realloc(ptr, 0) works like free");

    mem_deinit();
}

/* Throughput benchmark */
static void test_throughput(void) {
    mem_init();
    mm_init();

    int ops = 100000;
    void *ptrs[1000];
    memset(ptrs, 0, sizeof(ptrs));

    clock_t start = clock();

    unsigned int seed = 42;
    for (int i = 0; i < ops; i++) {
        int idx = (int)(seed % 1000);
        seed = seed * 1103515245 + 12345; /* simple LCG */

        if (ptrs[idx] != NULL) {
            mm_free(ptrs[idx]);
            ptrs[idx] = NULL;
        } else {
            size_t size = 16 + (seed % 512);
            seed = seed * 1103515245 + 12345;
            ptrs[idx] = mm_malloc(size);
        }
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    double kops = (ops / 1000.0) / elapsed;

    printf("  Throughput: %.0f KOPS (%.3f seconds for %d ops)\n", kops, elapsed, ops);

    if (kops > 0)
        PASS("throughput benchmark completed");
    else
        FAIL("throughput benchmark completed");

    /* Cleanup */
    for (int i = 0; i < 1000; i++) {
        if (ptrs[i] != NULL)
            mm_free(ptrs[i]);
    }

    mem_deinit();
}

int main(void) {
    printf("=== Stress Tests ===\n");

    test_many_small();
    test_alternating();
    test_realloc();
    test_throughput();

    printf("\nResults: %d failure(s)\n", failures);
    return failures > 0 ? 1 : 0;
}
