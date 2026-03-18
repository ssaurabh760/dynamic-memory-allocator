# A Beginner's Guide to Dynamic Memory Allocation

This guide explains what a dynamic memory allocator is, why it exists, and how this project implements one from scratch in C. No prior systems programming knowledge is assumed.

---

## Table of Contents

1. [What Is Memory?](#1-what-is-memory)
2. [Stack vs Heap](#2-stack-vs-heap)
3. [What Is malloc?](#3-what-is-malloc)
4. [Why Build Your Own Allocator?](#4-why-build-your-own-allocator)
5. [How the Heap Is Organized](#5-how-the-heap-is-organized)
6. [Block Structure](#6-block-structure)
7. [Allocation: Finding Free Space](#7-allocation-finding-free-space)
8. [Freeing Memory](#8-freeing-memory)
9. [Coalescing: Merging Free Blocks](#9-coalescing-merging-free-blocks)
10. [Splitting: Avoiding Waste](#10-splitting-avoiding-waste)
11. [The Free List](#11-the-free-list)
12. [Footer Elimination](#12-footer-elimination)
13. [Realloc: Resizing Blocks](#13-realloc-resizing-blocks)
14. [Fragmentation](#14-fragmentation)
15. [Putting It All Together](#15-putting-it-all-together)
16. [Glossary](#16-glossary)

---

## 1. What Is Memory?

When a program runs, it needs space to store things: variables, arrays, strings, data structures. This space is called **memory** (specifically, RAM). You can think of memory as a giant array of bytes, where each byte has an address (like a house number on a street).

```
Address:  0x0000   0x0001   0x0002   0x0003   ...
          ┌────────┬────────┬────────┬────────┐
Memory:   │  byte  │  byte  │  byte  │  byte  │  ...
          └────────┴────────┴────────┴────────┘
```

Programs don't get direct access to all of physical memory. The operating system gives each program its own **virtual address space** — a private view of memory.

---

## 2. Stack vs Heap

A program's memory is divided into several regions. The two most important for understanding allocators are the **stack** and the **heap**.

```
High addresses
┌──────────────────────┐
│       Stack          │  <- Local variables, function calls
│    (grows down)      │     Automatically managed
├──────────────────────┤
│                      │
│    (unused space)    │
│                      │
├──────────────────────┤
│       Heap           │  <- Dynamically allocated memory
│    (grows up)        │     Manually managed
├──────────────────────┤
│   Data (globals)     │
├──────────────────────┤
│   Code (text)        │
└──────────────────────┘
Low addresses
```

### The Stack
- Stores local variables and function call information
- Managed automatically: when a function returns, its variables disappear
- Very fast but limited in size
- You cannot control its lifetime

### The Heap
- Stores data that needs to live beyond a single function call
- You control when memory is allocated and freed
- Much larger than the stack
- Slower to allocate from (this is what our allocator speeds up)

**Example:** If you want to read a file into memory and pass that data around your program, you need heap memory because the data must outlive the function that reads it.

---

## 3. What Is malloc?

In C, you request heap memory using `malloc()`:

```c
// Ask for 100 bytes of heap memory
char *buffer = malloc(100);

// Use the memory
strcpy(buffer, "Hello, world!");

// When done, give it back
free(buffer);
```

`malloc(size)` does three things:
1. Finds a chunk of free memory on the heap that is at least `size` bytes
2. Marks that chunk as "in use" so no one else gets it
3. Returns a pointer to the start of the usable space

`free(ptr)` does the reverse: it marks that chunk as available again.

**The key question is:** How does malloc find free space efficiently? That is exactly what this project implements.

---

## 4. Why Build Your Own Allocator?

The system's `malloc` (from glibc or similar) is highly optimized but also very complex. Building one from scratch teaches you:

- How programs interact with the operating system for memory
- How data structures can be embedded inside raw memory
- The tradeoffs between speed (throughput) and memory efficiency (utilization)
- Low-level concepts: pointer arithmetic, alignment, bitwise operations

This project implements a simplified but realistic allocator that manages a simulated heap.

---

## 5. How the Heap Is Organized

Our allocator gets raw memory from the system using `sbrk()` (or in our case, a simulated `mem_sbrk()`). This function extends the heap by a given number of bytes.

The heap is organized as a sequence of **blocks**. Each block is either **allocated** (in use) or **free** (available):

```
┌───────────┬───────────┬───────────┬───────────┬───────────┬──────────┐
│ Prologue  │  Block A  │  Block B  │  Block C  │  Block D  │ Epilogue │
│ (barrier) │ ALLOCATED │   FREE    │ ALLOCATED │   FREE    │ (barrier)│
└───────────┴───────────┴───────────┴───────────┴───────────┴──────────┘
```

- **Prologue**: A special always-allocated block at the start. It prevents the allocator from trying to look "before" the beginning of the heap.
- **Epilogue**: A special zero-size block at the end. It marks where the heap stops.
- **Regular blocks**: The actual memory being managed.

---

## 6. Block Structure

Every block has metadata (bookkeeping information) and payload (the space the user actually uses).

### Allocated Block

```
┌─────────────────────────────┐
│ Header (8 bytes)            │  <- Stores: block size + status bits
├─────────────────────────────┤
│                             │
│ Payload (user's data)       │  <- This is the pointer malloc returns
│                             │
└─────────────────────────────┘
```

### Free Block

```
┌─────────────────────────────┐
│ Header (8 bytes)            │  <- Stores: block size + status bits
├─────────────────────────────┤
│ Previous free pointer (8 B) │  <- Points to previous free block
├─────────────────────────────┤
│ Next free pointer (8 bytes) │  <- Points to next free block
├─────────────────────────────┤
│ (unused space)              │
├─────────────────────────────┤
│ Footer (8 bytes)            │  <- Copy of header (for reverse traversal)
└─────────────────────────────┘
```

### The Header

The header is a single 8-byte word that packs three pieces of information using **bitwise operations**:

```
Header value: 0x00000000 00000053
                              ││
                              │└─ bit 0: allocated? (1=yes, 0=no)
                              └── bit 1: previous block allocated? (1=yes, 0=no)
              └──────────────┘
                 block size (must be multiple of 16, so bottom 4 bits
                 are always 0 — we reuse bits 0-1 for flags)
```

For example, a header value of `0x53` means:
- Size = `0x50` = 80 bytes
- Previous block is allocated (bit 1 = 1)
- This block is allocated (bit 0 = 1)

This trick works because block sizes are always multiples of 16 (due to alignment), so the bottom 4 bits of the size are always zero. We steal two of those bits for flags.

### Why 16-Byte Alignment?

Modern CPUs work most efficiently when data is aligned to certain boundaries. On x86-64 systems, 16-byte alignment is standard because SSE instructions require it. Our allocator guarantees every returned pointer is a multiple of 16.

---

## 7. Allocation: Finding Free Space

When `mm_malloc(size)` is called, the allocator needs to:

1. **Calculate the actual block size needed**: The user asks for `size` bytes, but we also need room for the header (8 bytes) and must round up to a multiple of 16. The minimum block is 32 bytes (to hold free list pointers if the block is later freed).

2. **Search for a free block**: Walk through available free blocks looking for one that is large enough.

3. **Place the block**: Mark it as allocated and potentially split it.

### First-Fit Search

The simplest strategy: scan the free blocks from the beginning and take the first one that fits.

```
Free list:  [48 bytes] -> [32 bytes] -> [128 bytes] -> [64 bytes] -> NULL

Request: malloc(20)  →  Need 32-byte block  →  First fit: [48 bytes]
```

Our allocator uses **bounded first-fit**: it checks at most 50 free blocks. If none fit, it asks the OS for more memory. This prevents slow scans when there are thousands of small free blocks.

---

## 8. Freeing Memory

When `mm_free(ptr)` is called:

1. Look at the header (just before the pointer) to find the block size
2. Mark the block as free (clear bit 0 in the header)
3. Write a footer (copy of the header at the end of the block)
4. Tell the next block that we are now free (clear bit 1 in its header)
5. Try to merge with adjacent free blocks (coalescing)

```
Before free(B):
┌───────────┬───────────┬───────────┐
│  Block A  │  Block B  │  Block C  │
│ ALLOCATED │ ALLOCATED │ ALLOCATED │
└───────────┴───────────┴───────────┘

After free(B):
┌───────────┬───────────┬───────────┐
│  Block A  │  Block B  │  Block C  │
│ ALLOCATED │   FREE    │ ALLOCATED │
└───────────┴───────────┴───────────┘
```

---

## 9. Coalescing: Merging Free Blocks

**The problem:** If we free blocks one at a time without merging, we end up with many small free blocks that can't satisfy large requests, even though the total free space is sufficient.

```
Bad (no coalescing):
┌──────┬──────┬──────┬──────┬──────┐
│ FREE │ FREE │ FREE │ FREE │ FREE │   5 x 32 = 160 bytes free
│ 32 B │ 32 B │ 32 B │ 32 B │ 32 B │   but can't allocate 64 bytes!
└──────┴──────┴──────┴──────┴──────┘

Good (with coalescing):
┌──────────────────────────────────┐
│            FREE                  │   160 bytes free
│           160 bytes              │   can allocate up to 152 bytes
└──────────────────────────────────┘
```

**Coalescing** means: when a block is freed, check if its neighbors are also free, and if so, merge them into one big free block.

### The Four Cases

When block B is freed, its neighbors can be in four states:

```
Case 1: [ALLOC] [B freed] [ALLOC]  →  No merging needed
Case 2: [ALLOC] [B freed] [FREE ]  →  Merge B with next block
Case 3: [FREE ] [B freed] [ALLOC]  →  Merge B with previous block
Case 4: [FREE ] [B freed] [FREE ]  →  Merge all three into one
```

### How Do We Know if Neighbors Are Free?

- **Next block**: Easy. Add B's size to B's address to get the next block, then check its header.
- **Previous block**: This is why free blocks have **footers**. The footer of the previous block sits right before B's header. By reading it, we can find the previous block's size and walk backward.

But wait — what if the previous block is allocated? Allocated blocks don't have footers (we removed them to save space). That is where the `prev_alloc` bit comes in. If bit 1 in B's header is set, the previous block is allocated and we skip the check.

---

## 10. Splitting: Avoiding Waste

If we find a 256-byte free block but only need 32 bytes, it would be wasteful to use the entire block. Instead, we **split** it:

```
Before split:
┌─────────────────────────────────────────┐
│              FREE (256 bytes)            │
└─────────────────────────────────────────┘

After malloc(24) → need 32-byte block:
┌───────────────┬─────────────────────────┐
│  ALLOCATED    │       FREE              │
│   (32 bytes)  │     (224 bytes)         │
└───────────────┴─────────────────────────┘
```

We only split if the remainder is at least 32 bytes (the minimum block size). If the remainder is smaller, we just give the user the whole block — the few wasted bytes (internal fragmentation) are better than creating a useless tiny block.

---

## 11. The Free List

### Implicit Free List (Slow)

The simplest approach: scan every block in the heap (allocated and free) looking for a free block.

```
Scan: [ALLOC] → [ALLOC] → [FREE!] found it!
       skip       skip      ✓
```

This is O(total blocks), which gets slow as the heap grows. Even if there's only 1 free block among 10,000 allocated ones, we scan all 10,000.

### Explicit Free List (Fast)

Better approach: maintain a linked list of only the free blocks. Each free block stores pointers to the previous and next free blocks.

```
free_list_head → [Free Block A] ⇄ [Free Block B] ⇄ [Free Block C] → NULL
```

Now `malloc` only visits free blocks. If there are 10,000 blocks but only 5 are free, we only visit 5.

The pointers are stored in the payload area of the free block (since nobody is using that space anyway). This is why the minimum block size is 32 bytes: 8 (header) + 8 (prev ptr) + 8 (next ptr) + 8 (footer) = 32.

### LIFO Insertion

When a block is freed, we insert it at the **head** of the list (like pushing onto a stack). This is called LIFO (Last In, First Out).

Benefits:
- O(1) insertion — just update two pointers
- Recently freed blocks are found first — good for programs that free and immediately reallocate

---

## 12. Footer Elimination

### The Insight

Footers exist for one reason: so that when freeing block B, we can find the previous block's size (by reading the footer right before B's header) and coalesce.

But we only need to coalesce with the previous block if it is **free**. If it is allocated, we skip it entirely.

### The Optimization

We add a `prev_alloc` bit to each header. If it says "previous block is allocated," we know there is no footer to read and no coalescing to do.

This saves 8 bytes per allocated block. For a program with thousands of small allocations, this significantly improves memory utilization.

```
With footers (old):          Without footers (optimized):
┌────────┐                   ┌────────┐
│ Header │ 8 bytes           │ Header │ 8 bytes
├────────┤                   ├────────┤
│Payload │                   │Payload │
│  ...   │                   │  ...   │  ← 8 bytes more payload space!
├────────┤                   └────────┘
│ Footer │ 8 bytes
└────────┘
Overhead: 16 bytes           Overhead: 8 bytes
```

---

## 13. Realloc: Resizing Blocks

`realloc(ptr, new_size)` changes the size of an existing allocation. The naive approach is:

```
1. malloc(new_size)
2. memcpy(old data to new location)
3. free(old block)
```

This is slow because of the copy. Our allocator tries smarter approaches first:

### Strategy 1: Already Big Enough
If the current block is already large enough, just return it. If shrinking significantly, split off the excess as a new free block.

### Strategy 2: Absorb Next Block
If the next block in memory is free and the combined size is sufficient, merge them in place. No data movement needed.

```
Before realloc (grow):
┌───────────────┬───────────────┐
│  ALLOCATED    │     FREE      │
│ (our block)   │  (next block) │
└───────────────┴───────────────┘

After absorbing next:
┌───────────────────────────────┐
│        ALLOCATED              │
│    (our block, now bigger)    │
└───────────────────────────────┘
```

### Strategy 3: Absorb Previous Block
If the previous block is free, merge with it and shift data backward using `memmove`.

### Fallback
If none of the above work, fall back to malloc + memcpy + free.

---

## 14. Fragmentation

Fragmentation is the main enemy of memory allocators. There are two types:

### Internal Fragmentation
Wasted space **inside** an allocated block. If the user asks for 20 bytes but the minimum block is 32 bytes, 12 bytes are wasted.

```
┌────────┬──────────────┬────────┐
│ Header │   Payload    │ Wasted │
│ 8 B    │   20 bytes   │ 4 B   │
└────────┴──────────────┴────────┘
  Block size = 32 bytes, but only 20 used
```

### External Fragmentation
Free space exists in total, but is broken into pieces too small to use.

```
┌──────┬──────┬──────┬──────┬──────┬──────┐
│ALLOC │ FREE │ALLOC │ FREE │ALLOC │ FREE │
│ 32 B │ 16 B │ 32 B │ 16 B │ 32 B │ 16 B │
└──────┴──────┴──────┴──────┴──────┴──────┘
Total free: 48 bytes, but largest contiguous free: 16 bytes
Cannot satisfy malloc(32)!
```

Coalescing helps reduce external fragmentation. Good placement policies (first-fit, best-fit) also help.

---

## 15. Putting It All Together

Here is the complete lifecycle of memory in our allocator:

```
1. Program starts
   → mm_init() creates prologue, epilogue, extends heap with 4KB free block

2. malloc(100) called
   → Compute: need 112 bytes (100 + 8 header, rounded to 16)
   → Search free list: find a 4096-byte free block
   → Split: allocate 112 bytes, remaining 3984 bytes stay free
   → Return pointer to payload

3. malloc(200) called
   → Need 208 bytes
   → Search free list: find the 3984-byte free block
   → Split: allocate 208, remaining 3776 stays free

4. free(first pointer) called
   → Mark 112-byte block as free
   → Check neighbors: prev is prologue (allocated), next is 208-byte (allocated)
   → Case 1: no coalescing, insert into free list

5. free(second pointer) called
   → Mark 208-byte block as free
   → Check neighbors: prev is 112-byte (FREE!), next is 3776-byte (FREE!)
   → Case 4: coalesce all three into one 4096-byte free block

6. Heap is back to initial state (one big free block)
```

---

## 16. Glossary

| Term | Definition |
|------|-----------|
| **Alignment** | Requirement that addresses be multiples of a value (e.g., 16 bytes) |
| **Allocated** | A block currently in use by the program |
| **Block** | A contiguous chunk of memory with a header and payload |
| **Boundary tag** | Header/footer pair that allows bidirectional traversal |
| **Coalescing** | Merging adjacent free blocks into one larger block |
| **Epilogue** | Special zero-size block marking the end of the heap |
| **Explicit free list** | Linked list connecting only free blocks |
| **External fragmentation** | Free memory exists but is scattered in unusable pieces |
| **First-fit** | Allocation strategy: use the first free block that is large enough |
| **Footer** | Copy of header at the end of a block (only for free blocks in our allocator) |
| **Free** | A block not currently in use, available for allocation |
| **Header** | Metadata at the start of a block storing size and status |
| **Heap** | Region of memory for dynamic allocation, grown with sbrk |
| **Implicit free list** | Scanning all blocks (allocated and free) to find free space |
| **Internal fragmentation** | Wasted space inside an allocated block |
| **KOPS** | Thousands of operations per second (performance metric) |
| **LIFO** | Last In First Out — insertion policy for the free list |
| **Minimum block size** | Smallest possible block (32 bytes in our allocator) |
| **Payload** | The usable portion of an allocated block |
| **Prologue** | Special always-allocated block at the start of the heap |
| **sbrk** | System call to extend the heap |
| **Splitting** | Dividing a large free block when only part is needed |
| **Throughput** | How many operations the allocator can handle per second |
| **Utilization** | Ratio of useful payload to total heap size |

---

## Further Reading

- **CSAPP Chapter 9.9**: The classic textbook explanation of dynamic memory allocation (Bryant & O'Hallaron)
- **dlmalloc**: Doug Lea's real-world malloc implementation
- **glibc malloc internals**: How Linux's standard library implements malloc
- **jemalloc** / **tcmalloc**: Modern high-performance allocators used by Firefox and Google
