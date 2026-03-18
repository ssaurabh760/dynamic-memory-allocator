#include "memlib.h"
#include "mm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Trace-driven testing framework.
 *
 * Trace file format:
 *   a <id> <size>    — allocate <size> bytes, store pointer at index <id>
 *   f <id>           — free pointer at index <id>
 *   r <id> <size>    — realloc pointer at index <id> to <size> bytes
 *
 * Example:
 *   a 0 100
 *   a 1 200
 *   f 0
 *   r 1 400
 *   f 1
 */

#define MAX_PTRS 10000
#define MAX_LINE 256

static void *ptrs[MAX_PTRS];
static size_t sizes[MAX_PTRS]; /* track allocated sizes for validation */

static int run_trace(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "ERROR: Cannot open trace file: %s\n", filename);
        return -1;
    }

    mem_init();
    if (mm_init() != 0) {
        fprintf(stderr, "ERROR: mm_init failed\n");
        fclose(fp);
        mem_deinit();
        return -1;
    }

    memset(ptrs, 0, sizeof(ptrs));
    memset(sizes, 0, sizeof(sizes));

    char line[MAX_LINE];
    int line_num = 0;
    int ops = 0;
    int errors = 0;
    size_t peak_usage = 0;

    clock_t start = clock();

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        char op;
        int id;
        size_t size;

        /* Skip comments and blank lines */
        if (line[0] == '#' || line[0] == '\n')
            continue;

        if (sscanf(line, " %c", &op) != 1)
            continue;

        switch (op) {
        case 'a': /* allocate */
            if (sscanf(line, " a %d %zu", &id, &size) != 2) {
                fprintf(stderr, "  ERROR: bad alloc at line %d\n", line_num);
                errors++;
                continue;
            }
            if (id < 0 || id >= MAX_PTRS) {
                fprintf(stderr, "  ERROR: id %d out of range at line %d\n", id, line_num);
                errors++;
                continue;
            }
            ptrs[id] = mm_malloc(size);
            if (ptrs[id] == NULL) {
                fprintf(stderr, "  ERROR: mm_malloc(%zu) returned NULL at line %d\n", size,
                        line_num);
                errors++;
                continue;
            }
            /* Check alignment */
            if ((unsigned long)ptrs[id] % 16 != 0) {
                fprintf(stderr, "  ERROR: unaligned pointer at line %d\n", line_num);
                errors++;
            }
            /* Write a pattern to verify later */
            memset(ptrs[id], (unsigned char)(id & 0xFF), size);
            sizes[id] = size;
            ops++;

            /* Track peak */
            size_t heap_sz = mem_heapsize();
            if (heap_sz > peak_usage)
                peak_usage = heap_sz;
            break;

        case 'f': /* free */
            if (sscanf(line, " f %d", &id) != 1) {
                fprintf(stderr, "  ERROR: bad free at line %d\n", line_num);
                errors++;
                continue;
            }
            if (id < 0 || id >= MAX_PTRS) {
                fprintf(stderr, "  ERROR: id %d out of range at line %d\n", id, line_num);
                errors++;
                continue;
            }
            mm_free(ptrs[id]);
            ptrs[id] = NULL;
            sizes[id] = 0;
            ops++;
            break;

        case 'r': /* realloc */
            if (sscanf(line, " r %d %zu", &id, &size) != 2) {
                fprintf(stderr, "  ERROR: bad realloc at line %d\n", line_num);
                errors++;
                continue;
            }
            if (id < 0 || id >= MAX_PTRS) {
                fprintf(stderr, "  ERROR: id %d out of range at line %d\n", id, line_num);
                errors++;
                continue;
            }

            /* Verify old data before realloc */
            if (ptrs[id] != NULL && sizes[id] > 0) {
                unsigned char expected = (unsigned char)(id & 0xFF);
                unsigned char *cp = (unsigned char *)ptrs[id];
                size_t check_size = sizes[id] < size ? sizes[id] : size;
                for (size_t i = 0; i < check_size; i++) {
                    if (cp[i] != expected) {
                        fprintf(stderr, "  ERROR: data corruption at line %d (byte %zu)\n",
                                line_num, i);
                        errors++;
                        break;
                    }
                }
            }

            ptrs[id] = mm_realloc(ptrs[id], size);
            if (size > 0 && ptrs[id] == NULL) {
                fprintf(stderr, "  ERROR: mm_realloc returned NULL at line %d\n", line_num);
                errors++;
                continue;
            }
            if (ptrs[id] != NULL) {
                memset(ptrs[id], (unsigned char)(id & 0xFF), size);
                sizes[id] = size;
            } else {
                sizes[id] = 0;
            }
            ops++;
            break;

        default:
            fprintf(stderr, "  WARNING: unknown op '%c' at line %d\n", op, line_num);
            break;
        }
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    double kops = (ops / 1000.0) / (elapsed > 0 ? elapsed : 0.001);

    /* Run heap checker */
    if (!mm_check()) {
        fprintf(stderr, "  ERROR: mm_check failed after trace\n");
        errors++;
    }

    printf("  Trace: %-35s | Ops: %5d | KOPS: %8.0f | Heap: %6zu KB | Errors: %d\n", filename,
           ops, kops, peak_usage / 1024, errors);

    fclose(fp);
    mem_deinit();

    return errors;
}

int main(int argc, char **argv) {
    printf("=== Trace-Driven Tests ===\n");

    int total_errors = 0;

    if (argc > 1) {
        /* Run specified trace files */
        for (int i = 1; i < argc; i++) {
            int err = run_trace(argv[i]);
            if (err > 0)
                total_errors += err;
        }
    } else {
        /* Run all trace files in tests/traces/ */
        const char *traces[] = {"tests/traces/trace_random.txt",  "tests/traces/trace_coalesce.txt",
                                "tests/traces/trace_realloc.txt", "tests/traces/trace_binary.txt",
                                "tests/traces/trace_pathological.txt", NULL};

        for (int i = 0; traces[i] != NULL; i++) {
            int err = run_trace(traces[i]);
            if (err > 0)
                total_errors += err;
            else if (err < 0)
                total_errors++;
        }
    }

    printf("\nTotal errors: %d\n", total_errors);
    return total_errors > 0 ? 1 : 0;
}
