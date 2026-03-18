#!/bin/bash
# Benchmark script for dynamic memory allocator

echo "==============================="
echo " Memory Allocator Benchmarks"
echo "==============================="
echo ""

# Build with optimizations
make clean > /dev/null 2>&1
make all > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: Build failed"
    exit 1
fi

echo "Running stress tests with throughput measurement..."
echo ""
./test_stress

echo ""
echo "==============================="
echo " Benchmark Complete"
echo "==============================="
