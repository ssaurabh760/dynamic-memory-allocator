# Dynamic Memory Allocator

A high-performance `malloc`-like dynamic memory allocator implemented in C. Built from scratch using an explicit free list with boundary tag coalescing, block splitting, and footer elimination for allocated blocks. Supports full POSIX-compatible allocation semantics including `malloc`, `free`, `realloc`, and `calloc`.

## Performance

| Metric       | Target   | Achieved  |
|-------------|----------|-----------|
| Throughput  | 14,500 KOPS | ~30,000+ KOPS |
| Utilization | 78%      | 78%+      |

## Architecture

```
                           Heap Layout
  ┌──────────┬──────────┬──────────┬─────┬──────────┬──────────┐
  │ Padding  │ Prologue │ Block 1  │ ... │ Block N  │ Epilogue │
  │ (8 B)    │ (H|F)    │          │     │          │ (H only) │
  └──────────┴──────────┴──────────┴─────┴──────────┴──────────┘

  Allocated Block (no footer):        Free Block (with footer):
  ┌─────────────────────────────┐     ┌─────────────────────────────┐
  │ Header [size|prev_alloc|1]  │     │ Header [size|prev_alloc|0]  │
  ├─────────────────────────────┤     ├─────────────────────────────┤
  │                             │     │ Prev Free Pointer           │
  │ Payload                     │     ├─────────────────────────────┤
  │                             │     │ Next Free Pointer           │
  └─────────────────────────────┘     ├─────────────────────────────┤
                                      │ (unused space)              │
                                      ├─────────────────────────────┤
                                      │ Footer [size|prev_alloc|0]  │
                                      └─────────────────────────────┘

  Header bit layout: [ block size | prev_alloc (bit 1) | alloc (bit 0) ]
  Minimum block size: 32 bytes (header + prev_ptr + next_ptr + footer)
  Alignment: 16 bytes
```

### Free List Organization

Free blocks are maintained in a doubly-linked explicit free list with LIFO (stack) insertion policy. When a block is freed, it is inserted at the head of the list. This gives O(1) insertion and enables fast reuse of recently freed blocks.

```
  free_list_head
       │
       ▼
  ┌─────────┐     ┌─────────┐     ┌─────────┐
  │ Free Blk │────▶│ Free Blk │────▶│ Free Blk │────▶ NULL
  │  (new)   │◀────│  (old)   │◀────│ (oldest) │
  └─────────┘     └─────────┘     └─────────┘
```

## Project Structure

```
dynamic-memory-allocator/
├── src/
│   ├── mm.c              # Main allocator implementation
│   ├── mm.h              # Allocator API (mm_init, mm_malloc, mm_free, mm_realloc)
│   ├── memlib.c          # Simulated memory system (mem_sbrk, mem_init, etc.)
│   └── memlib.h          # Memory system interface
├── tests/
│   ├── test_basic.c      # Basic allocation/free correctness tests
│   ├── test_coalesce.c   # Coalescing tests (all 4 cases)
│   ├── test_stress.c     # Stress tests and throughput benchmark
│   ├── test_trace.c      # Trace-driven testing framework
│   └── traces/           # Trace files (random, coalesce, realloc, binary, pathological)
├── include/
│   └── config.h          # Tunable constants (ALIGNMENT, WSIZE, DSIZE, CHUNKSIZE)
├── scripts/
│   └── benchmark.sh      # Benchmarking script
├── Makefile
├── BENCHMARKS.md          # Performance results
├── DESIGN.md              # Detailed design rationale
└── README.md
```

## Build & Run

```bash
# Build all test executables
make all

# Run unit tests (basic, coalesce, stress)
make test

# Run trace-driven tests
make trace

# Build with AddressSanitizer for debugging
make debug

# Run benchmarks
make benchmark

# Clean build artifacts
make clean
```

## API

```c
int   mm_init(void);                  // Initialize the allocator
void *mm_malloc(size_t size);         // Allocate size bytes
void  mm_free(void *ptr);            // Free a previously allocated block
void *mm_realloc(void *ptr, size_t size); // Resize an allocated block
int   mm_check(void);                // Heap consistency checker
```

## Key Optimizations

1. **Footer elimination** — Allocated blocks store only a header (8 bytes overhead instead of 16). A `prev_alloc` bit in the next block's header indicates whether the previous block is allocated, removing the need to read a footer during coalescing.

2. **Bounded first-fit** — Searches at most 50 free blocks before extending the heap. Prevents O(n) worst-case scans while maintaining good utilization.

3. **Optimized realloc** — Tries three in-place strategies before falling back to malloc+copy+free:
   - Shrink in place with splitting
   - Absorb next free block
   - Absorb previous free block (with memmove)

4. **Explicit free list** — Only free blocks are searched during allocation, giving O(free blocks) search instead of O(all blocks) with an implicit list.

## Performance Comparison

| Version                   | Throughput (KOPS) | Search Complexity  |
|---------------------------|-------------------|--------------------|
| v0.1 Implicit free list   | ~1,500            | O(total blocks)    |
| v0.2 Explicit free list   | ~35,000           | O(free blocks)     |
| v0.3 Optimized            | ~30,000+          | O(min(free, 50))   |

## Implementation Roadmap

- [x] **v0.0** Project setup and infrastructure
- [x] **v0.1** Implicit free list allocator (baseline)
- [x] **v0.2** Explicit free list allocator
- [x] **v0.3** Performance optimization (footer elimination, bounded first-fit, realloc)
- [x] **v0.4** Stress testing with trace-driven framework and ASan validation
- [x] **v1.0** Documentation and final polish

## Testing

The allocator is validated with:
- **24 unit tests** across basic, coalescing, and stress test suites
- **5 trace files** covering random patterns, heavy coalescing, realloc workloads, binary allocation, and pathological fragmentation
- **AddressSanitizer** — zero memory errors
- **Heap consistency checker** (`mm_check`) verifying alignment, header/footer consistency, coalescing invariants, and free list integrity
