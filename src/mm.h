#ifndef MM_H
#define MM_H

#include <stddef.h>

/* Initialize the allocator. Returns 0 on success, -1 on failure. */
int mm_init(void);

/* Allocate a block of at least size bytes. Returns pointer to allocated
 * block payload, or NULL if allocation fails. */
void *mm_malloc(size_t size);

/* Free the block pointed to by ptr. */
void mm_free(void *ptr);

/* Resize the block pointed to by ptr to size bytes.
 * Returns pointer to the reallocated block, or NULL on failure. */
void *mm_realloc(void *ptr, size_t size);

/* Heap consistency checker. Returns non-zero if heap is consistent. */
int mm_check(void);

#endif /* MM_H */
