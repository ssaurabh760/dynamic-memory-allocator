#include "memlib.h"
#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private global variables */
static char *mem_heap;      /* Points to the first byte of the heap */
static char *mem_brk;       /* Points to the last byte of the heap + 1 */
static char *mem_max_addr;  /* Max legal heap addr + 1 */

/*
 * mem_init - Initialize the memory system by allocating a large block
 *            of memory via malloc to simulate the heap.
 */
void mem_init(void) {
    mem_heap = (char *)malloc(MAX_HEAP_SIZE);
    if (mem_heap == NULL) {
        fprintf(stderr, "mem_init: malloc failed to allocate %d bytes\n", MAX_HEAP_SIZE);
        exit(1);
    }
    mem_brk = mem_heap;
    mem_max_addr = mem_heap + MAX_HEAP_SIZE;
}

/*
 * mem_deinit - Free the storage used by the memory system.
 */
void mem_deinit(void) {
    free(mem_heap);
    mem_heap = NULL;
    mem_brk = NULL;
    mem_max_addr = NULL;
}

/*
 * mem_sbrk - Extends the heap by incr bytes and returns the start address
 *            of the new area. Semantics identical to Unix sbrk.
 *            Returns (void *)-1 on error.
 */
void *mem_sbrk(int incr) {
    char *old_brk = mem_brk;

    if ((incr < 0) || ((mem_brk + incr) > mem_max_addr)) {
        errno = ENOMEM;
        fprintf(stderr, "mem_sbrk: out of memory (requested %d bytes)\n", incr);
        return (void *)-1;
    }

    mem_brk += incr;
    return (void *)old_brk;
}

/*
 * mem_reset_brk - Reset the simulated brk pointer to make an empty heap.
 */
void mem_reset_brk(void) {
    mem_brk = mem_heap;
}

/*
 * mem_heap_lo - Return address of the first heap byte.
 */
void *mem_heap_lo(void) {
    return (void *)mem_heap;
}

/*
 * mem_heap_hi - Return address of the last heap byte.
 */
void *mem_heap_hi(void) {
    return (void *)(mem_brk - 1);
}

/*
 * mem_heapsize - Return the current size of the heap in bytes.
 */
size_t mem_heapsize(void) {
    return (size_t)(mem_brk - mem_heap);
}
