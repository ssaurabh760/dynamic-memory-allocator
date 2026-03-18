#include "mm.h"
#include "config.h"
#include "memlib.h"
#include <string.h>

/* Single word (8) or double word (16) alignment */
#define ALIGNMENT 16

/*
 * Block structure (implicit free list):
 *
 * Allocated block:          Free block:
 * +----------------+        +----------------+
 * | Header [size|a] |        | Header [size|0] |
 * +----------------+        +----------------+
 * |    Payload     |        |                |
 * |      ...       |        |   Free space   |
 * +----------------+        |                |
 * | Footer [size|a] |        +----------------+
 * +----------------+        | Footer [size|0] |
 *                           +----------------+
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

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Private variables */
static char *heap_listp = NULL; /* Pointer to first block */

/* Private function prototypes */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

/*
 * mm_init - Initialize the memory allocator.
 *           Creates the initial empty heap with prologue and epilogue blocks.
 * Returns 0 on success, -1 on failure.
 */
int mm_init(void) {
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
        asize = 2 * DSIZE;
    else
        asize = ALIGN(size + DSIZE);

    /* Search the free list for a fit */
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
 * Four cases:
 *   Case 1: prev allocated, next allocated -> no coalescing
 *   Case 2: prev allocated, next free -> merge with next
 *   Case 3: prev free, next allocated -> merge with prev
 *   Case 4: prev free, next free -> merge with both
 */
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        /* Case 1: both neighbors allocated */
        return bp;
    } else if (prev_alloc && !next_alloc) {
        /* Case 2: merge with next block */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {
        /* Case 3: merge with previous block */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else {
        /* Case 4: merge with both neighbors */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}

/*
 * find_fit - Find a free block that fits the requested size (first-fit).
 */
static void *find_fit(size_t asize) {
    void *bp;

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (GET_SIZE(HDRP(bp)) >= asize)) {
            return bp;
        }
    }
    return NULL; /* No fit */
}

/*
 * place - Place the requested block at the beginning of the free block,
 *         splitting if the remainder is at least the minimum block size.
 */
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));

    if ((csize - asize) >= MIN_BLOCK_SIZE) {
        /* Split the block */
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    } else {
        /* Use the entire block */
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * mm_check - Heap consistency checker (stub for now).
 * Returns non-zero if the heap is consistent.
 */
int mm_check(void) {
    /* TODO: Implement in Deliverable 1 */
    return 1;
}
