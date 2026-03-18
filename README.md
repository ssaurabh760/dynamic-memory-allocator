# Dynamic Memory Allocator

A high-performance `malloc`-like dynamic memory allocator implemented in C, using explicit free lists, boundary tag coalescing, and block splitting strategies.

## Goals

- **Memory utilization**: >= 78% peak utilization
- **Throughput**: >= 14,500 KOPS (thousands of operations per second)

## Architecture

```
Heap Layout:
+----------+----------+----------+     +----------+---------+
| Prologue | Block 1  | Block 2  | ... | Block N  | Epilogue|
| (H|F)    | (H|P|F)  | (H|P|F)  |     | (H|P|F)  | (H)     |
+----------+----------+----------+     +----------+---------+

Block Structure (Allocated):     Block Structure (Free):
+---------------------------+    +---------------------------+
| Header: [size | alloc]    |    | Header: [size | 0]        |
+---------------------------+    +---------------------------+
| Payload                   |    | Free space                |
| ...                       |    | ...                       |
+---------------------------+    +---------------------------+
| Footer: [size | alloc]    |    | Footer: [size | 0]        |
+---------------------------+    +---------------------------+
```

## Project Structure

```
dynamic-memory-allocator/
├── src/
│   ├── mm.c          # Main allocator implementation
│   ├── mm.h          # Allocator function prototypes
│   ├── memlib.c      # Memory system simulator (sbrk wrapper)
│   └── memlib.h      # Memory system interface
├── tests/
│   ├── test_basic.c      # Basic allocation tests
│   ├── test_coalesce.c   # Coalescing tests
│   ├── test_stress.c     # Stress/throughput tests
│   └── traces/           # Trace files for benchmarking
├── include/
│   └── config.h      # Tunable constants
├── scripts/
│   └── benchmark.sh  # Benchmarking script
├── Makefile
└── README.md
```

## Build & Run

```bash
# Build all test executables
make all

# Run all tests
make test

# Build with debug flags (AddressSanitizer enabled)
make debug

# Run benchmarks
make benchmark

# Clean build artifacts
make clean
```

## Implementation Roadmap

- [x] **Deliverable 0**: Project setup and infrastructure
- [ ] **Deliverable 1**: Implicit free list allocator (baseline)
- [ ] **Deliverable 2**: Explicit free list allocator
- [ ] **Deliverable 3**: Performance optimization
- [ ] **Deliverable 4**: Stress testing and edge cases
- [ ] **Deliverable 5**: Documentation and final polish

## Key Concepts

- **Implicit Free List**: Linear scan through all blocks to find free space
- **Explicit Free List**: Doubly-linked list of only free blocks for faster search
- **Boundary Tag Coalescing**: Headers and footers enable O(1) merging of adjacent free blocks
- **Block Splitting**: Large free blocks are split when only part is needed
- **First-Fit Search**: Allocate from the first block that fits the request
