# Benchmarks

## Test Environment
- Platform: macOS (Darwin 23.6.0)
- Compiler: gcc with -O2 -Wall -Werror -Wextra
- Heap size: 64 MB simulated

## Throughput Results (100,000 mixed malloc/free operations)

| Allocator Version         | Throughput (KOPS) | Search Complexity  |
|---------------------------|-------------------|--------------------|
| Implicit free list (v0.1) | ~1,500            | O(total blocks)    |
| Explicit free list (v0.2) | ~35,000           | O(free blocks)     |
| Optimized (v0.3+)         | ~34,000           | O(min(free, 50))   |

**Target: 14,500 KOPS** | **Achieved: ~34,000 KOPS** (2.3x target)

## Trace-Driven Results

| Trace File          | Operations | Throughput (KOPS) | Peak Heap | Errors |
|---------------------|-----------|-------------------|-----------|--------|
| trace_random.txt    | 56        | 538               | 12 KB     | 0      |
| trace_coalesce.txt  | 42        | 857               | 4 KB      | 0      |
| trace_realloc.txt   | 40        | 816               | 4 KB      | 0      |
| trace_binary.txt    | 70        | 1,429             | 4 KB      | 0      |
| trace_pathological  | 44        | 1,467             | 27 KB     | 0      |

## Optimizations Applied

1. **Footer elimination**: Reduces per-block overhead from 16 to 8 bytes for allocated blocks
2. **Bounded first-fit (50)**: Caps search at 50 free blocks for consistent O(1) amortized allocation
3. **In-place realloc**: Shrink, absorb-next, absorb-prev strategies before malloc+copy+free fallback
4. **Explicit free list with LIFO**: O(1) insertion, searches only free blocks

## Test Summary

| Test Suite     | Tests | Result |
|----------------|-------|--------|
| Basic          | 11    | 11/11 PASS |
| Coalesce       | 5     | 5/5 PASS   |
| Stress         | 8     | 8/8 PASS   |
| Trace-driven   | 5     | 5/5 PASS   |
| AddressSanitizer | all | Zero errors |
