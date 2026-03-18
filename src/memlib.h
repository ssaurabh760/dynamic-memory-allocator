#ifndef MEMLIB_H
#define MEMLIB_H

#include <stddef.h>

/* Initialize the memory system */
void mem_init(void);

/* Deinitialize the memory system */
void mem_deinit(void);

/* Extend the heap by incr bytes and return the start address of the new area.
 * Returns (void *)-1 on error. */
void *mem_sbrk(int incr);

/* Reset the simulated brk pointer to make an empty heap */
void mem_reset_brk(void);

/* Returns the address of the first byte in the heap */
void *mem_heap_lo(void);

/* Returns the address of the last byte in the heap */
void *mem_heap_hi(void);

/* Returns the current size of the heap in bytes */
size_t mem_heapsize(void);

#endif /* MEMLIB_H */
