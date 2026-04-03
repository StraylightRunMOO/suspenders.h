#!/bin/bash

# Build script for libsuspenders

set -e

echo "========================================"
echo "  libsuspenders Build Script"
echo "========================================"

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo ""
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo ""
echo "Building..."
make -j$(nproc)

echo ""
echo "========================================"
echo "  Build Complete!"
echo "========================================"
echo ""
echo "Binaries:"
echo "  ./build/suspenders-demo       - Coroutine demo"
echo "  ./build/suspenders-benchmark  - Context switch benchmark"
echo "  ./build/suspenders-tests      - Unit tests"
echo ""
echo "Usage:"
echo "  ./build/suspenders-demo                    # Run demo"
echo "  ./build/suspenders-tests                   # Run unit tests"
echo "  ./build/suspenders-benchmark 1000000       # Run benchmark"
echo ""
