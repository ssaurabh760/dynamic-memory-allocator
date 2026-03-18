#include "mm.h"
#include "config.h"
#include "memlib.h"
#include <stdio.h>
#include <string.h>

/* Single word (8) or double word (16) alignment */
#define ALIGNMENT 16

/*
 * Block structure (explicit free list):
 *
 * Allocated block:              Free block:
 * +------------------------+    +------------------------+
 * | Header [size | alloc]  |    | Header [size | 0]      |
 * +------------------------+    +------------------------+
 * | Payload                |    | Prev free ptr          |
 * | ...                    |    +------------------------+
 * +------------------------+    | Next free ptr          |
 * | Footer [size | alloc]  |    +------------------------+
 * +------------------------+    | (unused space)         |
 *                               +------------------------+
 *                               | Footer [size | 0]      |
 *                               +------------------------+
 *
 * Minimum block size = header(8) + prev(8) + next(8) + footer(8) = 32 bytes
 */

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned long *)(p))
#define PUT(p, val) (*(unsigned long *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0xFUL)
#define GET_ALLOC(p) (GET(p) & 0x1UL)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks in heap */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Explicit free list: prev/next pointers stored in first 16 bytes of payload */
#define GET_PREV_FREE(bp) (*(void **)(bp))
#define GET_NEXT_FREE(bp) (*(void **)((char *)(bp) + WSIZE))
#define SET_PREV_FREE(bp, ptr) (*(void **)(bp) = (ptr))
#define SET_NEXT_FREE(bp, ptr) (*(void **)((char *)(bp) + WSIZE) = (ptr))

/* Private variables */
static char *heap_listp = NULL;    /* Pointer to prologue block */
static void *free_list_head = NULL; /* Head of explicit free list */

/* Private function prototypes */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void free_list_insert(void *bp);
static void free_list_remove(void *bp);

/*
 * free_list_insert - Insert a free block at the head of the free list (LIFO).
 */
static void free_list_insert(void *bp) {
    SET_PREV_FREE(bp, NULL);
    SET_NEXT_FREE(bp, free_list_head);

    if (free_list_head != NULL) {
        SET_PREV_FREE(free_list_head, bp);
    }

    free_list_head = bp;
}

/*
 * free_list_remove - Remove a block from the explicit free list.
 */
static void free_list_remove(void *bp) {
    void *prev = GET_PREV_FREE(bp);
    void *next = GET_NEXT_FREE(bp);

    if (prev != NULL) {
        SET_NEXT_FREE(prev, next);
    } else {
        free_list_head = next;
    }

    if (next != NULL) {
        SET_PREV_FREE(next, prev);
    }
}

/*
 * mm_init - Initialize the memory allocator.
 *           Creates the initial empty heap with prologue and epilogue blocks.
 * Returns 0 on success, -1 on failure.
 */
int mm_init(void) {
    free_list_head = NULL;

    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);                            /* Alignment padding */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     /* Epilogue header */
    heap_listp += (2 * WSIZE);

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

/*
 * mm_malloc - Allocate a block with at least size bytes of payload.
 * Searches the explicit free list for a fitting block.
 * Returns pointer to allocated block payload, or NULL on failure.
 */
void *mm_malloc(size_t size) {
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;

    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment */
    if (size <= DSIZE)
        asize = 2 * DSIZE; /* Minimum block size = 32 */
    else
        asize = ALIGN(size + DSIZE);

    /* Ensure minimum block size for free list pointers */
    if (asize < MIN_BLOCK_SIZE)
        asize = MIN_BLOCK_SIZE;

    /* Search the explicit free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = asize > CHUNKSIZE ? asize : CHUNKSIZE;
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Free a block and coalesce with adjacent free blocks.
 * The resulting free block is inserted into the explicit free list.
 */
void mm_free(void *ptr) {
    if (ptr == NULL)
        return;

    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - Resize a previously allocated block.
 */
void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL)
        return mm_malloc(size);

    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    size_t oldsize = GET_SIZE(HDRP(ptr)) - DSIZE; /* payload size */
    size_t asize;

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = ALIGN(size + DSIZE);

    if (asize < MIN_BLOCK_SIZE)
        asize = MIN_BLOCK_SIZE;

    size_t block_size = GET_SIZE(HDRP(ptr));

    /* Current block is large enough */
    if (block_size >= asize) {
        return ptr;
    }

    /* Try to absorb the next block if it's free */
    char *next_bp = NEXT_BLKP(ptr);
    size_t next_size = GET_SIZE(HDRP(next_bp));
    int next_alloc = GET_ALLOC(HDRP(next_bp));

    if (!next_alloc && (block_size + next_size) >= asize) {
        /* Remove next block from free list before absorbing */
        free_list_remove(next_bp);
        size_t combined = block_size + next_size;
        PUT(HDRP(ptr), PACK(combined, 1));
        PUT(FTRP(ptr), PACK(combined, 1));
        return ptr;
    }

    /* Fallback: allocate new block, copy, and free old */
    void *newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

    size_t copysize = oldsize < size ? oldsize : size;
    memcpy(newptr, ptr, copysize);
    mm_free(ptr);
    return newptr;
}

/*
 * extend_heap - Extend the heap by the given number of words.
 * The new free block is inserted into the explicit free list.
 * Returns pointer to the new free block, or NULL on failure.
 */
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if (size < MIN_BLOCK_SIZE)
        size = MIN_BLOCK_SIZE;

    if ((long)(bp = mem_sbrk((int)size)) == -1)
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         /* Free block header */
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}

/*
 * coalesce - Merge adjacent free blocks using boundary tag coalescing.
 * Removes coalesced neighbors from the free list before merging,
 * then inserts the resulting block into the free list.
 *
 * Four cases:
 *   Case 1: prev allocated, next allocated -> just insert
 *   Case 2: prev allocated, next free -> merge with next
 *   Case 3: prev free, next allocated -> merge with prev
 *   Case 4: prev free, next free -> merge with both
 */
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        /* Case 1: both neighbors allocated — just insert */
    } else if (prev_alloc && !next_alloc) {
        /* Case 2: merge with next block */
        free_list_remove(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {
        /* Case 3: merge with previous block */
        free_list_remove(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else {
        /* Case 4: merge with both neighbors */
        free_list_remove(PREV_BLKP(bp));
        free_list_remove(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    /* Insert the coalesced block into the free list */
    free_list_insert(bp);
    return bp;
}

/*
 * find_fit - Search the explicit free list for a block that fits (first-fit).
 */
static void *find_fit(size_t asize) {
    void *bp;

    for (bp = free_list_head; bp != NULL; bp = GET_NEXT_FREE(bp)) {
        if (GET_SIZE(HDRP(bp)) >= asize) {
            return bp;
        }
    }
    return NULL; /* No fit */
}

/*
 * place - Place the requested block at the beginning of the free block,
 *         splitting if the remainder is at least the minimum block size.
 *         Removes block from free list; if split, inserts remainder.
 */
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));

    /* Remove this block from the free list */
    free_list_remove(bp);

    if ((csize - asize) >= MIN_BLOCK_SIZE) {
        /* Split the block */
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *split_bp = NEXT_BLKP(bp);
        PUT(HDRP(split_bp), PACK(csize - asize, 0));
        PUT(FTRP(split_bp), PACK(csize - asize, 0));
        /* Insert the remainder into the free list */
        free_list_insert(split_bp);
    } else {
        /* Use the entire block */
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * mm_check - Heap consistency checker for explicit free list.
 * Checks:
 *   1. Every free block is properly coalesced (no adjacent free blocks)
 *   2. Headers match footers for every block
 *   3. All blocks lie within heap boundaries
 *   4. All blocks are properly aligned
 *   5. Every free block in heap is in the free list
 *   6. Every block in the free list is actually marked free
 *   7. Free list has no cycles (tortoise and hare)
 * Returns non-zero if the heap is consistent, 0 on error.
 */
int mm_check(void) {
    char *bp;
    char *lo = (char *)mem_heap_lo();
    char *hi = (char *)mem_heap_hi();
    int prev_free = 0;
    int heap_free_count = 0;
    int list_free_count = 0;

    /* Check prologue block */
    if (GET_SIZE(HDRP(heap_listp)) != DSIZE || !GET_ALLOC(HDRP(heap_listp))) {
        fprintf(stderr, "mm_check: bad prologue header\n");
        return 0;
    }
    if (GET(HDRP(heap_listp)) != GET(FTRP(heap_listp))) {
        fprintf(stderr, "mm_check: prologue header != footer\n");
        return 0;
    }

    /* Iterate through all blocks in the heap */
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        /* Check block lies within heap boundaries */
        if ((char *)bp < lo || (char *)bp > hi) {
            fprintf(stderr, "mm_check: block %p outside heap [%p, %p]\n", (void *)bp, (void *)lo,
                    (void *)hi);
            return 0;
        }

        /* Check alignment */
        if ((unsigned long)bp % ALIGNMENT != 0) {
            fprintf(stderr, "mm_check: block %p not %d-byte aligned\n", (void *)bp, ALIGNMENT);
            return 0;
        }

        /* Check header matches footer */
        if (GET(HDRP(bp)) != GET(FTRP(bp))) {
            fprintf(stderr, "mm_check: header != footer at block %p\n", (void *)bp);
            return 0;
        }

        /* Check no two consecutive free blocks (coalescing invariant) */
        int curr_free = !GET_ALLOC(HDRP(bp));
        if (prev_free && curr_free) {
            fprintf(stderr, "mm_check: consecutive free blocks at %p\n", (void *)bp);
            return 0;
        }
        prev_free = curr_free;

        if (curr_free)
            heap_free_count++;
    }

    /* Check epilogue */
    if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp))) {
        fprintf(stderr, "mm_check: bad epilogue at %p\n", (void *)bp);
        return 0;
    }

    /* Validate free list: check every entry is actually free, and count entries */
    void *slow = free_list_head;
    void *fast = free_list_head;
    for (bp = free_list_head; bp != NULL; bp = GET_NEXT_FREE(bp)) {
        /* Check block in free list is marked free */
        if (GET_ALLOC(HDRP(bp))) {
            fprintf(stderr, "mm_check: block %p in free list but marked allocated\n", (void *)bp);
            return 0;
        }

        /* Check block in free list is within heap */
        if ((char *)bp < lo || (char *)bp > hi) {
            fprintf(stderr, "mm_check: free list block %p outside heap\n", (void *)bp);
            return 0;
        }

        list_free_count++;

        /* Cycle detection (tortoise and hare) */
        slow = GET_NEXT_FREE(slow);
        if (fast != NULL)
            fast = GET_NEXT_FREE(fast);
        if (fast != NULL)
            fast = GET_NEXT_FREE(fast);
        if (slow != NULL && fast != NULL && slow == fast) {
            fprintf(stderr, "mm_check: cycle detected in free list\n");
            return 0;
        }
    }

    /* Check that free list count matches heap free block count */
    if (heap_free_count != list_free_count) {
        fprintf(stderr, "mm_check: heap has %d free blocks but free list has %d\n",
                heap_free_count, list_free_count);
        return 0;
    }

    return 1;
}
