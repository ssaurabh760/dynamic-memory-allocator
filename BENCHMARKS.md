# Benchmarks

## Test Environment
- Platform: macOS (Darwin 23.6.0)
- Compiler: gcc with -O2
- Heap size: 64 MB simulated

## Throughput Results (100,000 operations)

| Allocator Version         | Throughput (KOPS) | Notes                                   |
|---------------------------|-------------------|-----------------------------------------|
| Implicit free list (v0.1) | ~1,500            | Linear scan through all blocks          |
| Explicit free list (v0.2) | ~35,000           | Linked list of free blocks only         |
| Optimized (v0.3)          | ~34,000           | Footer elimination + bounded first-fit  |

## Optimizations Applied (v0.3)

1. **Footer elimination**: Allocated blocks have no footer; prev_alloc bit stored in next block's header. Reduces per-block overhead from 16 bytes to 8 bytes.
2. **Bounded first-fit**: Searches at most 50 free blocks before giving up and extending heap. Prevents worst-case O(n) scans.
3. **In-place realloc**: Three strategies before fallback copy:
   - Shrink in place with splitting
   - Absorb next free block
   - Absorb previous free block (with memmove)
4. **Adaptive heap extension**: Extends by max(request, CHUNKSIZE) to reduce sbrk calls.

## Test Results

- Basic tests: 11/11 PASS
- Coalesce tests: 5/5 PASS
- Stress tests: 8/8 PASS
- AddressSanitizer: Zero errors
