# Design Document

## Overview

This allocator manages a simulated heap using `mem_sbrk()`, organizing memory into blocks with headers and (for free blocks) footers. An explicit doubly-linked free list tracks available blocks for O(free) allocation search.

## Free List Organization

### Why Explicit Over Implicit?

An **implicit free list** stores size/alloc info in every block header and searches all blocks (allocated + free) during `malloc`. This gives O(total blocks) search time, which degrades badly as the heap grows.

An **explicit free list** adds prev/next pointers to free blocks only, linking them into a separate list. Allocation searches only free blocks: O(free blocks) time. The overhead is zero for allocated blocks since the pointers live in unused payload space.

### Why Not Segregated Free Lists?

Segregated lists bin free blocks by size class for O(1) average-case search. However, they add significant implementation complexity (multiple lists, size class management, inter-class splitting). For our target performance (14,500 KOPS, 78% utilization), a single explicit list with bounded first-fit achieves both goals with simpler, more maintainable code.

### Insertion Policy: LIFO

New free blocks are inserted at the head of the list. This has two benefits:
1. **O(1) insertion** — no list traversal needed
2. **Temporal locality** — recently freed blocks are found first, improving cache behavior in malloc-heavy workloads

## Coalescing Algorithm

When a block is freed, it checks its neighbors and merges adjacent free blocks. With footer elimination, the algorithm uses two mechanisms to find neighbors:

- **Next block**: Found by adding current block's size to its address (always available via header)
- **Previous block**: Found by reading the footer of the previous block (only exists if previous block is free, indicated by `prev_alloc` bit)

### The Four Cases

```
Case 1: [ALLOC] [freed] [ALLOC]    → No merge, just insert into free list
Case 2: [ALLOC] [freed] [free ]    → Merge with next, remove next from list
Case 3: [free ] [freed] [ALLOC]    → Merge with prev, remove prev from list
Case 4: [free ] [freed] [free ]    → Merge all three, remove both from list
```

In all cases, the resulting free block is inserted at the head of the free list.

## Splitting Policy

When a free block is larger than the requested size, we split it if the remainder is at least 32 bytes (minimum block size = header + prev_ptr + next_ptr + footer). The allocated portion is placed at the **beginning** of the free block, and the remainder becomes a new free block inserted into the free list.

If the remainder is smaller than 32 bytes, we allocate the entire block to avoid creating unusably small fragments (internal fragmentation is preferable to external fragmentation of tiny blocks).

## Footer Elimination

### The Problem

Standard boundary-tag coalescing requires both header and footer on every block (16 bytes overhead per block). For small allocations, this overhead is significant — a 16-byte payload requires a 48-byte block (33% utilization).

### The Solution

Allocated blocks don't need footers. The only reason footers exist is so the **next** block can find the previous block's size during coalescing. But if the previous block is allocated, we never need to coalesce with it.

We add a `prev_alloc` bit to each block's header:

```
Header: [ block_size (upper bits) | prev_alloc (bit 1) | alloc (bit 0) ]
```

- If `prev_alloc == 1`: previous block is allocated, no footer exists, no coalescing needed
- If `prev_alloc == 0`: previous block is free, footer exists, can read size and coalesce

This reduces per-block overhead from 16 bytes to 8 bytes for allocated blocks.

### Maintenance

The `prev_alloc` bit must be updated whenever a block's allocation status changes:
- `mm_malloc` → set next block's `prev_alloc = 1`
- `mm_free` → set next block's `prev_alloc = 0`
- `coalesce` → update after merging
- `place` (split) → set remainder's and post-remainder's `prev_alloc` correctly

## Realloc Optimization Strategy

`mm_realloc` tries to avoid malloc+memcpy+free by attempting in-place operations:

### Case 0: Current block is large enough
If the block already has sufficient space (including after shrinking), return it directly. If shrinking creates enough remainder (>= 32 bytes), split and free the tail.

### Case 1: Absorb next block
If the next block is free and the combined size is sufficient, merge them in place. No data movement needed.

### Case 2: Absorb previous block
If the previous block is free and combined size is sufficient, merge and `memmove` data backward. Also tries combining with both previous and next blocks.

### Fallback
If no in-place strategy works, fall back to malloc + memcpy + free.

## Bounded First-Fit Search

Pure first-fit searches the entire free list, which can degrade to O(n) in fragmented heaps. We limit the search to **50 blocks** maximum. If no fit is found within 50 blocks, we extend the heap.

This trades a small amount of utilization (might miss a perfect fit deeper in the list) for consistent O(1) amortized allocation time. In practice, the LIFO insertion policy means recently freed blocks (which are often good fits) appear near the head of the list.

## Constants and Tuning

| Constant       | Value    | Rationale                                          |
|----------------|----------|----------------------------------------------------|
| ALIGNMENT      | 16 bytes | Standard x86-64 alignment for SSE instructions     |
| WSIZE          | 8 bytes  | Word size (header/footer/pointer size on 64-bit)   |
| DSIZE          | 16 bytes | Double word                                        |
| CHUNKSIZE      | 4096     | Default heap extension size (1 page)               |
| MIN_BLOCK_SIZE | 32 bytes | header(8) + prev_ptr(8) + next_ptr(8) + footer(8)  |
| MAX_SEARCH     | 50       | Bounded first-fit search limit                     |

## Tradeoffs

| Decision                  | Pro                           | Con                              |
|---------------------------|-------------------------------|----------------------------------|
| Explicit over segregated  | Simpler, fewer bugs           | Slightly slower for varied sizes |
| LIFO insertion            | O(1), good locality           | May increase fragmentation       |
| Footer elimination        | 50% less overhead per block   | More complex bit management      |
| Bounded first-fit (50)    | Consistent O(1) allocation    | May miss better fits             |
| 32-byte minimum block     | Supports free list pointers   | More internal fragmentation      |
