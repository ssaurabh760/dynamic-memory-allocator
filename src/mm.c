#include "mm.h"
#include "config.h"
#include "memlib.h"
#include <stdio.h>
#include <string.h>

/*
 * Optimized dynamic memory allocator with explicit free list.
 *
 * Key optimizations:
 *   - Footer elimination for allocated blocks (prev_alloc bit in next header)
 *   - Bounded first-fit search (max 50 free blocks checked)
 *   - In-place realloc with shrink, next-absorb, and prev-absorb
 *   - Adaptive heap extension sizing
 *
 * Block layout:
 *
 *   Allocated block:                   Free block:
 *   +-----------------------------+    +-----------------------------+
 *   | Header [size|prev_alloc|1]  |    | Header [size|prev_alloc|0]  |
 *   +-----------------------------+    +-----------------------------+
 *   | Payload                     |    | Prev free ptr               |
 *   | ...                         |    +-----------------------------+
 *   +-----------------------------+    | Next free ptr               |
 *   (NO footer for allocated)          +-----------------------------+
 *                                      | ...                         |
 *                                      +-----------------------------+
 *                                      | Footer [size|prev_alloc|0]  |
 *                                      +-----------------------------+
 *
 * Header bit layout: [ size (upper bits) | prev_alloc (bit 1) | alloc (bit 0) ]
 * Minimum block size = header(8) + prev_ptr(8) + next_ptr(8) + footer(8) = 32 bytes
 */

/* Pack size, prev_alloc, and alloc into a word */
#define PACK(size, prev_alloc, alloc) ((size) | ((prev_alloc) << 1) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned long *)(p))
#define PUT(p, val) (*(unsigned long *)(p) = (val))

/* Read fields from address p */
#define GET_SIZE(p) (GET(p) & ~0xFUL)
#define GET_ALLOC(p) (GET(p) & 0x1UL)
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2UL) >> 1)

/* Given block ptr bp, compute address of its header */
#define HDRP(bp) ((char *)(bp) - WSIZE)

/* Footer: only exists for free blocks */
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next block */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))

/* Previous block — only valid when prev_alloc == 0 (prev has footer) */
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Explicit free list pointers */
#define GET_PREV_FREE(bp) (*(void **)(bp))
#define GET_NEXT_FREE(bp) (*(void **)((char *)(bp) + WSIZE))
#define SET_PREV_FREE(bp, ptr) (*(void **)(bp) = (ptr))
#define SET_NEXT_FREE(bp, ptr) (*(void **)((char *)(bp) + WSIZE) = (ptr))

/* Bounded first-fit: max blocks to search */
#define MAX_SEARCH 50

/* Private variables */
static char *heap_listp = NULL;
static void *free_list_head = NULL;

/* Private function prototypes */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void free_list_insert(void *bp);
static void free_list_remove(void *bp);

/*
 * set_prev_alloc - Set or clear the prev_alloc bit (bit 1) in bp's header.
 * This is essential for footer elimination: allocated blocks have no footer,
 * so the next block's header must record whether the previous block is allocated.
 */
static inline void set_prev_alloc(void *bp, int prev_alloc) {
    if (prev_alloc)
        PUT(HDRP(bp), GET(HDRP(bp)) | 0x2UL);
    else
        PUT(HDRP(bp), GET(HDRP(bp)) & ~0x2UL);
}

/*
 * free_list_insert - Insert a free block at the head of the free list (LIFO).
 * LIFO policy gives O(1) insertion and good temporal locality — recently
 * freed blocks appear first in subsequent searches.
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
 * free_list_remove - Unlink a block from the doubly-linked explicit free list.
 * Handles both middle-of-list and head-of-list cases.
 */
static void free_list_remove(void *bp) {
    void *prev = GET_PREV_FREE(bp);
    void *next = GET_NEXT_FREE(bp);

    if (prev != NULL)
        SET_NEXT_FREE(prev, next);
    else
        free_list_head = next;

    if (next != NULL)
        SET_PREV_FREE(next, prev);
}

/*
 * mm_init - Initialize the allocator. Creates the initial heap with:
 *   - 8-byte alignment padding
 *   - Prologue block (header + footer, always allocated, acts as boundary)
 *   - Epilogue header (zero-size, always allocated, marks end of heap)
 * Then extends the heap with CHUNKSIZE bytes of free space.
 *
 * Returns 0 on success, -1 on failure.
 */
int mm_init(void) {
    free_list_head = NULL;

    /* Allocate space for padding + prologue (header+footer) + epilogue header */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);                                    /* Alignment padding */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1, 1));     /* Prologue header */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1, 1));     /* Prologue footer */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1, 1));         /* Epilogue header */
    heap_listp += (2 * WSIZE); /* Point to prologue payload (between header and footer) */

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

/*
 * mm_malloc - Allocate a block with at least 'size' bytes of payload.
 *
 * 1. Compute adjusted block size (add header overhead, enforce minimum and alignment)
 * 2. Search explicit free list using bounded first-fit (max 50 blocks)
 * 3. If fit found, place block (may split) and return payload pointer
 * 4. If no fit, extend heap and place
 *
 * Returns pointer to payload on success, NULL on failure.
 */
void *mm_malloc(size_t size) {
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0)
        return NULL;

    /* Adjust block size: header only (no footer for allocated blocks) */
    if (size <= (DSIZE + WSIZE))
        asize = MIN_BLOCK_SIZE; /* 32 bytes minimum */
    else
        asize = ALIGN(size + WSIZE);

    if (asize < MIN_BLOCK_SIZE)
        asize = MIN_BLOCK_SIZE;

    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    extendsize = asize > CHUNKSIZE ? asize : CHUNKSIZE;
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Free a block and coalesce with adjacent free blocks.
 *
 * Steps:
 * 1. Mark block as free in header and write footer
 * 2. Clear prev_alloc bit in the next block's header
 * 3. If next block is also free, sync its footer with updated header
 * 4. Coalesce with adjacent free blocks
 */
void mm_free(void *ptr) {
    if (ptr == NULL)
        return;

    size_t size = GET_SIZE(HDRP(ptr));
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, prev_alloc, 0));
    PUT(FTRP(ptr), PACK(size, prev_alloc, 0));

    /* Tell next block that we are now free */
    set_prev_alloc(NEXT_BLKP(ptr), 0);
    /* If next block is free, update its footer too */
    char *next = NEXT_BLKP(ptr);
    if (!GET_ALLOC(HDRP(next)) && GET_SIZE(HDRP(next)) > 0) {
        PUT(FTRP(next), GET(HDRP(next)));
    }

    coalesce(ptr);
}

/*
 * mm_realloc - Resize a previously allocated block to 'size' bytes.
 *
 * Optimization strategy (tries in order to minimize copying):
 *   Case 0: Block already large enough — return as-is, split if possible
 *   Case 1: Next block is free — absorb it in-place (no data movement)
 *   Case 2: Previous block is free — absorb and memmove data backward
 *   Fallback: malloc new block, memcpy data, free old block
 *
 * Special cases: realloc(NULL, size) = malloc(size), realloc(ptr, 0) = free(ptr)
 */
void *mm_realloc(void *ptr, size_t size) {
    if (ptr == NULL)
        return mm_malloc(size);

    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    size_t block_size = GET_SIZE(HDRP(ptr));
    size_t oldpayload = block_size - WSIZE; /* header only, no footer */
    size_t asize;

    if (size <= (DSIZE + WSIZE))
        asize = MIN_BLOCK_SIZE;
    else
        asize = ALIGN(size + WSIZE);
    if (asize < MIN_BLOCK_SIZE)
        asize = MIN_BLOCK_SIZE;

    /* Case 0: Current block is large enough */
    if (block_size >= asize) {
        /* Split if remainder is large enough */
        size_t remainder = block_size - asize;
        if (remainder >= MIN_BLOCK_SIZE) {
            size_t pa = GET_PREV_ALLOC(HDRP(ptr));
            PUT(HDRP(ptr), PACK(asize, pa, 1));

            char *split_bp = NEXT_BLKP(ptr);
            PUT(HDRP(split_bp), PACK(remainder, 1, 0));
            PUT(FTRP(split_bp), PACK(remainder, 1, 0));

            set_prev_alloc(NEXT_BLKP(split_bp), 0);
            char *after = NEXT_BLKP(split_bp);
            if (!GET_ALLOC(HDRP(after)) && GET_SIZE(HDRP(after)) > 0)
                PUT(FTRP(after), GET(HDRP(after)));

            free_list_insert(split_bp);
        }
        return ptr;
    }

    /* Case 1: Absorb next block if free */
    char *next_bp = NEXT_BLKP(ptr);
    size_t next_size = GET_SIZE(HDRP(next_bp));
    int next_alloc = GET_ALLOC(HDRP(next_bp));

    if (!next_alloc && (block_size + next_size) >= asize) {
        free_list_remove(next_bp);
        size_t combined = block_size + next_size;
        size_t pa = GET_PREV_ALLOC(HDRP(ptr));
        PUT(HDRP(ptr), PACK(combined, pa, 1));

        set_prev_alloc(NEXT_BLKP(ptr), 1);

        /* Split if remainder is large enough */
        size_t remainder = combined - asize;
        if (remainder >= MIN_BLOCK_SIZE) {
            PUT(HDRP(ptr), PACK(asize, pa, 1));
            char *split_bp = NEXT_BLKP(ptr);
            PUT(HDRP(split_bp), PACK(remainder, 1, 0));
            PUT(FTRP(split_bp), PACK(remainder, 1, 0));
            set_prev_alloc(NEXT_BLKP(split_bp), 0);
            char *after = NEXT_BLKP(split_bp);
            if (!GET_ALLOC(HDRP(after)) && GET_SIZE(HDRP(after)) > 0)
                PUT(FTRP(after), GET(HDRP(after)));
            free_list_insert(split_bp);
        }

        return ptr;
    }

    /* Case 2: Absorb previous block if free */
    size_t pa_flag = GET_PREV_ALLOC(HDRP(ptr));
    if (!pa_flag) {
        char *prev_bp = PREV_BLKP(ptr);
        size_t prev_size = GET_SIZE(HDRP(prev_bp));
        size_t combined = prev_size + block_size;

        /* Also try combining with next if free */
        if (!next_alloc)
            combined += next_size;

        if (combined >= asize) {
            if (!next_alloc)
                free_list_remove(next_bp);
            free_list_remove(prev_bp);

            size_t pp = GET_PREV_ALLOC(HDRP(prev_bp));
            size_t copy_size = oldpayload < size ? oldpayload : size;
            memmove(prev_bp, ptr, copy_size);

            PUT(HDRP(prev_bp), PACK(combined, pp, 1));
            set_prev_alloc(NEXT_BLKP(prev_bp), 1);

            /* Split if remainder is large enough */
            size_t remainder = combined - asize;
            if (remainder >= MIN_BLOCK_SIZE) {
                PUT(HDRP(prev_bp), PACK(asize, pp, 1));
                char *split_bp = NEXT_BLKP(prev_bp);
                PUT(HDRP(split_bp), PACK(remainder, 1, 0));
                PUT(FTRP(split_bp), PACK(remainder, 1, 0));
                set_prev_alloc(NEXT_BLKP(split_bp), 0);
                char *after = NEXT_BLKP(split_bp);
                if (!GET_ALLOC(HDRP(after)) && GET_SIZE(HDRP(after)) > 0)
                    PUT(FTRP(after), GET(HDRP(after)));
                free_list_insert(split_bp);
            }

            return prev_bp;
        }
    }

    /* Fallback: malloc + memcpy + free */
    void *newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

    size_t copysize = oldpayload < size ? oldpayload : size;
    memcpy(newptr, ptr, copysize);
    mm_free(ptr);
    return newptr;
}

/*
 * extend_heap - Grow the heap by 'words' words via mem_sbrk.
 *
 * The new space overwrites the old epilogue header (becoming the new block's
 * header position), and a new epilogue is placed at the end. The prev_alloc
 * bit is inherited from the old epilogue. The new free block is then coalesced
 * with the previous block if it was free.
 *
 * Returns pointer to the new free block, or NULL on failure.
 */
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if (size < MIN_BLOCK_SIZE)
        size = MIN_BLOCK_SIZE;

    if ((long)(bp = mem_sbrk((int)size)) == -1)
        return NULL;

    /* Get prev_alloc from old epilogue */
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    PUT(HDRP(bp), PACK(size, prev_alloc, 0));  /* Free block header */
    PUT(FTRP(bp), PACK(size, prev_alloc, 0));  /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 0, 1));  /* New epilogue */

    return coalesce(bp);
}

/*
 * coalesce - Merge bp with adjacent free blocks using boundary tag coalescing.
 *
 * Uses prev_alloc bit (from bp's header) and next block's alloc bit to determine
 * which of the four coalescing cases applies. Removes merged neighbors from the
 * free list before merging, then inserts the resulting block.
 *
 * Cases:
 *   1: [alloc][bp][alloc] — no merge
 *   2: [alloc][bp][free ] — merge with next
 *   3: [free ][bp][alloc] — merge with prev (requires prev's footer)
 *   4: [free ][bp][free ] — merge with both
 *
 * Returns pointer to the coalesced free block.
 */
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        /* Case 1 */
    } else if (prev_alloc && !next_alloc) {
        /* Case 2: merge with next */
        char *next = NEXT_BLKP(bp);
        free_list_remove(next);
        size += GET_SIZE(HDRP(next));
        PUT(HDRP(bp), PACK(size, prev_alloc, 0));
        PUT(FTRP(bp), PACK(size, prev_alloc, 0));
    } else if (!prev_alloc && next_alloc) {
        /* Case 3: merge with prev */
        char *prev = PREV_BLKP(bp);
        free_list_remove(prev);
        size_t pp = GET_PREV_ALLOC(HDRP(prev));
        size += GET_SIZE(HDRP(prev));
        PUT(HDRP(prev), PACK(size, pp, 0));
        PUT(FTRP(prev), PACK(size, pp, 0));
        bp = prev;
    } else {
        /* Case 4: merge with both */
        char *next = NEXT_BLKP(bp);
        char *prev = PREV_BLKP(bp);
        free_list_remove(prev);
        free_list_remove(next);
        size_t pp = GET_PREV_ALLOC(HDRP(prev));
        size += GET_SIZE(HDRP(prev)) + GET_SIZE(HDRP(next));
        PUT(HDRP(prev), PACK(size, pp, 0));
        PUT(FTRP(prev), PACK(size, pp, 0));
        bp = prev;
    }

    /* Update next block's prev_alloc = 0 */
    set_prev_alloc(NEXT_BLKP(bp), 0);
    char *after = NEXT_BLKP(bp);
    if (!GET_ALLOC(HDRP(after)) && GET_SIZE(HDRP(after)) > 0) {
        PUT(FTRP(after), GET(HDRP(after)));
    }

    free_list_insert(bp);
    return bp;
}

/*
 * find_fit - Search the explicit free list for a block >= asize bytes.
 * Uses bounded first-fit: checks at most MAX_SEARCH (50) blocks to avoid
 * O(n) scans in fragmented heaps. Returns NULL if no fit found within the limit.
 */
static void *find_fit(size_t asize) {
    void *bp;
    int count = 0;

    for (bp = free_list_head; bp != NULL && count < MAX_SEARCH; bp = GET_NEXT_FREE(bp)) {
        if (GET_SIZE(HDRP(bp)) >= asize) {
            return bp;
        }
        count++;
    }
    return NULL;
}

/*
 * place - Allocate asize bytes from free block bp.
 *
 * 1. Remove bp from the free list
 * 2. If (block_size - asize) >= MIN_BLOCK_SIZE, split: allocate the front
 *    portion and create a new free block from the remainder
 * 3. Otherwise, use the entire block (accept internal fragmentation)
 * 4. Update prev_alloc bits in subsequent blocks
 */
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    free_list_remove(bp);

    if ((csize - asize) >= MIN_BLOCK_SIZE) {
        /* Split */
        PUT(HDRP(bp), PACK(asize, prev_alloc, 1));

        void *split_bp = NEXT_BLKP(bp);
        PUT(HDRP(split_bp), PACK(csize - asize, 1, 0));  /* prev is allocated */
        PUT(FTRP(split_bp), PACK(csize - asize, 1, 0));

        set_prev_alloc(NEXT_BLKP(split_bp), 0);
        char *after = NEXT_BLKP(split_bp);
        if (!GET_ALLOC(HDRP(after)) && GET_SIZE(HDRP(after)) > 0)
            PUT(FTRP(after), GET(HDRP(after)));

        free_list_insert(split_bp);
    } else {
        /* Use entire block */
        PUT(HDRP(bp), PACK(csize, prev_alloc, 1));
        set_prev_alloc(NEXT_BLKP(bp), 1);
    }
}

/*
 * mm_check - Heap consistency checker. Validates:
 *   1. Prologue and epilogue blocks are well-formed
 *   2. All blocks lie within heap boundaries and are 16-byte aligned
 *   3. Free blocks have matching header and footer
 *   4. No two adjacent blocks are both free (coalescing invariant)
 *   5. Every free block in the heap appears in the free list (and vice versa)
 *
 * Returns 1 if heap is consistent, 0 on error (prints diagnostics to stderr).
 */
int mm_check(void) {
    char *bp;
    char *lo = (char *)mem_heap_lo();
    char *hi = (char *)mem_heap_hi();
    int heap_free_count = 0;
    int list_free_count = 0;
    int prev_free = 0;

    /* Check prologue */
    if (GET_SIZE(HDRP(heap_listp)) != DSIZE || !GET_ALLOC(HDRP(heap_listp))) {
        fprintf(stderr, "mm_check: bad prologue\n");
        return 0;
    }

    /* Walk the heap */
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if ((char *)bp < lo || (char *)bp > hi) {
            fprintf(stderr, "mm_check: block %p outside heap\n", (void *)bp);
            return 0;
        }

        if ((unsigned long)bp % ALIGNMENT != 0) {
            fprintf(stderr, "mm_check: block %p not aligned\n", (void *)bp);
            return 0;
        }

        int curr_free = !GET_ALLOC(HDRP(bp));

        if (curr_free) {
            /* Check header matches footer */
            if (GET(HDRP(bp)) != GET(FTRP(bp))) {
                fprintf(stderr, "mm_check: header != footer at %p\n", (void *)bp);
                return 0;
            }
            if (prev_free) {
                fprintf(stderr, "mm_check: consecutive free blocks at %p\n", (void *)bp);
                return 0;
            }
            heap_free_count++;
        }

        prev_free = curr_free;
    }

    /* Check epilogue */
    if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp))) {
        fprintf(stderr, "mm_check: bad epilogue\n");
        return 0;
    }

    /* Count free list entries */
    for (bp = free_list_head; bp != NULL; bp = GET_NEXT_FREE(bp)) {
        if (GET_ALLOC(HDRP(bp))) {
            fprintf(stderr, "mm_check: allocated block in free list\n");
            return 0;
        }
        list_free_count++;
    }

    if (heap_free_count != list_free_count) {
        fprintf(stderr, "mm_check: heap has %d free, list has %d\n",
                heap_free_count, list_free_count);
        return 0;
    }

    return 1;
}
